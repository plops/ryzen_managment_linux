#include "analysis_manager.hpp"
#include <spdlog/spdlog.h>
#include <numeric>
#include <algorithm> // For std::sort

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
// Define it here, local to the function that uses it .
struct CoreCorrelationResult {
    int core_id = -1;
    double abs_diff = 0.0;
    size_t on_samples = 0;
    size_t off_samples = 0;

    // Sort in descending order of correlation strength
    bool operator<(const CoreCorrelationResult& other) const {
        return abs_diff > other.abs_diff;
    }
};


// This is the "cold path" - it runs only when the user clicks the button.
void AnalysisManager::run_correlation_analysis(const StressTester* stress_tester) {
    if (!stress_tester || !stress_tester->is_running()) {
        spdlog::warn("Analysis skipped: stress tester is not running.");
        return;
    }

    spdlog::info("Running correlation analysis...");
    std::lock_guard<std::mutex> lock(results_mutex_);
    if (analysis_results_.empty() || analysis_results_[0].history.empty()) {
        spdlog::warn("Analysis skipped: not enough historical data yet.");
        return;
    }

    const auto& periods = stress_tester->get_periods();
    const auto stress_start_time = stress_tester->get_start_time();
    const long long stress_start_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stress_start_time.time_since_epoch()).count();

    for (size_t i = 0; i < analysis_results_.size(); ++i) {
        auto& cell = analysis_results_[i];

        // --- Step 1: Calculate correlation strength for ALL cores ---
        std::vector<CoreCorrelationResult> core_results; // Now the type is defined
        for (int core_id = 0; core_id < stress_tester->get_core_count(); ++core_id) {
            const long long period_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(periods[core_id]).count();
            const long long work_duration_ns = period_ns / 3;

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

            core_results.push_back({core_id, std::abs(on_mean - off_mean), on_state_samples.size(), off_state_samples.size()});
        }

        // --- Step 2: Analyze the results to find the best, normalize, and calculate quality ---
        if (core_results.empty()) {
            cell.dominant_core_id = -1;
            cell.correlation_strength = 0.0f;
            cell.correlation_quality = 0.0f;
            continue;
        }

        // Sort to easily find the best and second-best
        std::sort(core_results.begin(), core_results.end());
        const auto& best_result = core_results[0];

        cell.dominant_core_id = best_result.core_id;

        // Normalize strength based on signal range
        double range = cell.max_val - cell.min_val;
        if (range > 1e-6) {
            cell.correlation_strength = static_cast<float>(std::min(1.0, best_result.abs_diff / range));
        } else {
            cell.correlation_strength = 0.0f;
        }

        // --- Step 3: Calculate the Quality Indicator ---

        // A) Separation Factor: How much better is the best vs. the second best?
        double separation_factor = 1.0; // Assume perfect separation by default
        if (core_results.size() > 1) {
            const auto& second_best = core_results[1];
            if (best_result.abs_diff > 1e-6) {
                // Formula: 1 - (second_best / best)
                separation_factor = 1.0 - (second_best.abs_diff / best_result.abs_diff);
            } else {
                separation_factor = 0.0; // If best is zero, there's no separation
            }
        }

        // B) Confidence Factor: Is the sample count high enough?
        const double TARGET_SAMPLES_PER_STATE = 30.0; // A reasonable number for statistical confidence
        size_t min_samples = std::min(best_result.on_samples, best_result.off_samples);
        double confidence_factor = std::min(1.0, min_samples / TARGET_SAMPLES_PER_STATE);

        // Final Quality = Separation * Confidence
        cell.correlation_quality = static_cast<float>(separation_factor * confidence_factor);
    }
    spdlog::info("Correlation analysis complete.");
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