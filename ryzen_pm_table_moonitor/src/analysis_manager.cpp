#include "analysis_manager.hpp"
#include <spdlog/spdlog.h>
#include <numeric>
#include <algorithm> // For std::sort
#include <thread>    // For std::this_thread::sleep_for

// This is the "hot path" - it runs for every sample from the PM Table.
void AnalysisManager::process_data_packet(const TimestampedData& current_data) {
    std::lock_guard<std::mutex> results_lock(results_mutex_);
    if (analysis_results_.size() != current_data.data.size()) {
        analysis_results_.assign(current_data.data.size(), CellStats());
    }

    for (size_t i = 0; i < current_data.data.size(); ++i) {
        analysis_results_[i].add_sample(current_data.data[i], current_data.timestamp_ns);
    }
}

// A small struct to hold the analysis result for a single core's stress pattern.
struct SingleCoreCorrelationResult {
    int core_id = -1;
    double correlation_strength = 0.0;
    size_t on_samples = 0;
    size_t off_samples = 0;

    // Sort in descending order of correlation strength
    bool operator<(const SingleCoreCorrelationResult& other) const {
        return correlation_strength > other.correlation_strength;
    }
};


// ENTIRELY REWRITTEN to perform sequential single-core analysis.
void AnalysisManager::run_correlation_analysis(StressTester* stress_tester) {
    if (!stress_tester || !stress_tester->is_running()) {
        spdlog::warn("Analysis skipped: stress tester is not running.");
        return;
    }

    std::lock_guard<std::mutex> lock(results_mutex_);
    if (analysis_results_.empty()) {
        spdlog::warn("Analysis skipped: no data yet.");
        return;
    }

    spdlog::info("Starting sequential correlation analysis to avoid crosstalk...");

    const unsigned int num_cores = stress_tester->get_core_count();
    const auto& periods = stress_tester->get_periods();

    // 1. Save the original stress state to restore it later.
    std::vector<bool> original_stress_states;
    original_stress_states.reserve(num_cores);
    for (unsigned int i = 0; i < num_cores; ++i) {
        original_stress_states.push_back(stress_tester->get_thread_busy_state(i));
    }

    // This vector will hold all results before they are sorted and trimmed.
    // Access via: results_per_cell[cell_index]
    std::vector<std::vector<SingleCoreCorrelationResult>> results_per_cell(analysis_results_.size());

    // 2. Main loop: iterate through each core, stressing it exclusively.
    for (unsigned int core_to_stress = 0; core_to_stress < num_cores; ++core_to_stress) {
        spdlog::info("Stressing ONLY core {}/{} to gather clean data...", core_to_stress, num_cores - 1);

        // 2a. Set the stress state: only the current core is active.
        for (unsigned int i = 0; i < num_cores; ++i) {
            stress_tester->set_thread_busy_state(i, i == core_to_stress);
        }

        // 2b. CRITICAL: Wait for the history buffers in CellStats to be populated
        // with data generated *only* during this single-core stress period.
        // The history size is 2000 samples and the sample rate is ~1ms, so ~2 seconds are needed.
        // We wait for 2.5s to be safe.
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));

        // 2c. Now that the history is "clean", calculate correlation for all cells
        // against this one active core.
        const auto stress_start_time = stress_tester->get_start_time();
        const long long stress_start_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stress_start_time.time_since_epoch()).count();
        const long long period_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(periods[core_to_stress]).count();
        const long long work_duration_ns = period_ns / 3;

        for (size_t i = 0; i < analysis_results_.size(); ++i) {
            auto& cell = analysis_results_[i];
            if (cell.history.empty()) continue;

            std::vector<float> on_state_samples;
            std::vector<float> off_state_samples;

            for (const auto& sample : cell.history) {
                long long time_since_start = sample.timestamp_ns - stress_start_time_ns;
                if (time_since_start < 0) continue;

                long long phase_in_period = time_since_start % period_ns;
                if (phase_in_period < work_duration_ns) {
                    on_state_samples.push_back(sample.value);
                } else {
                    off_state_samples.push_back(sample.value);
                }
            }

            if (on_state_samples.empty() || off_state_samples.empty()) continue;

            double on_mean = std::accumulate(on_state_samples.begin(), on_state_samples.end(), 0.0) / on_state_samples.size();
            double off_mean = std::accumulate(off_state_samples.begin(), off_state_samples.end(), 0.0) / off_state_samples.size();
            double strength = std::abs(on_mean - off_mean);

            // Normalize strength based on the signal's observed range.
            double range = cell.max_val - cell.min_val;
            double normalized_strength = 0.0;
            if (range > 1e-6) {
                normalized_strength = std::min(1.0, strength / range);
            }

            results_per_cell[i].push_back({(int)core_to_stress, normalized_strength, on_state_samples.size(), off_state_samples.size()});
        }
    }

    // 3. Post-processing: Aggregate results, sort, and update the final CellStats.
    spdlog::info("Aggregating and sorting results from all cores...");
    for (size_t i = 0; i < analysis_results_.size(); ++i) {
        auto& cell = analysis_results_[i];
        auto& all_core_results_for_cell = results_per_cell[i];

        // Sort all correlation results for this cell to find the strongest.
        std::sort(all_core_results_for_cell.begin(), all_core_results_for_cell.end());

        // Update the cell's official list with the top 4.
        cell.top_correlations.clear();
        for (size_t j = 0; j < 4 && j < all_core_results_for_cell.size(); ++j) {
            const auto& result = all_core_results_for_cell[j];

            // Calculate a simple quality metric based on sample count.
            const double TARGET_SAMPLES_PER_STATE = 30.0;
            size_t min_samples = std::min(result.on_samples, result.off_samples);
            double confidence_factor = std::min(1.0, min_samples / TARGET_SAMPLES_PER_STATE);

            cell.top_correlations.push_back({result.core_id, static_cast<float>(result.correlation_strength), static_cast<float>(confidence_factor)});
        }
    }

    // 4. Restore the original stress tester state as a final step.
    spdlog::info("Restoring original stress tester state.");
    for (unsigned int i = 0; i < num_cores; ++i) {
        stress_tester->set_thread_busy_state(i, original_stress_states[i]);
    }

    spdlog::info("Sequential correlation analysis complete.");
}

void AnalysisManager::reset_stats() {
    spdlog::info("Resetting statistics...");
    std::lock_guard<std::mutex> lock(results_mutex_);
    for (auto& stats : analysis_results_) {
        stats.reset();
    }
}

std::vector<CellStats> AnalysisManager::get_analysis_results() {
    std::lock_guard<std::mutex> lock(results_mutex_);
    return analysis_results_;
}