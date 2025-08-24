#include "analysis_manager.hpp"
#include <spdlog/spdlog.h>
#include <numeric>
#include <algorithm> // For std::sort and std::find_if
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

/**
 * @brief Main analysis function, revised for accuracy and live updates.
 *
 * This version implements a more robust "baseline vs. active" measurement.
 * For each core:
 * 1.  It measures the "baseline" volatility of all signals while the core is idle.
 * 2.  It then measures the "active" volatility while stressing that single core.
 * 3.  The correlation is calculated based on the *increase* in volatility.
 *
 * After each core is measured, it locks the shared results and updates them in-place,
 * ensuring the GUI reflects the new findings immediately and correctly.
 */
void AnalysisManager::run_correlation_analysis(StressTester* stress_tester) {
    spdlog::info("Starting improved correlation analysis...");
    const int core_count = stress_tester->get_core_count();
    const auto baseline_duration = std::chrono::milliseconds(1500);
    const auto active_duration = std::chrono::seconds(2);

    // Ensure all stress threads are initially off before we begin.
    for (int i = 0; i < core_count; ++i) {
        stress_tester->set_thread_busy_state(i, false);
    }

    // Loop through each core to stress it individually.
    for (int stressed_core_id = 0; stressed_core_id < core_count; ++stressed_core_id) {
        spdlog::info("Analysis: Measuring core {}...", stressed_core_id);

        // --- Step 1: Baseline Measurement (Core Idle) ---
        // The target core is already idle from the previous step or initial state.

        // Clear history to only capture baseline data.
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            for (auto& result : analysis_results_) {
                result.history.clear();
            }
        }
        std::this_thread::sleep_for(baseline_duration);

        // Capture the standard deviation of every cell during the idle period.
        std::vector<float> baseline_stddevs(analysis_results_.size());
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            for (size_t i = 0; i < analysis_results_.size(); ++i) {
                baseline_stddevs[i] = analysis_results_[i].get_stddev();
            }
        }

        // --- Step 2: Active Measurement (Core Stressed) ---
        // Activate only the target core.
        stress_tester->set_thread_busy_state(stressed_core_id, true);

        // Clear history again to only capture the active period data.
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            for (auto& result : analysis_results_) {
                result.history.clear();
            }
        }
        std::this_thread::sleep_for(active_duration);

        // Capture the standard deviation during the active period.
        std::vector<float> active_stddevs(analysis_results_.size());
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            for (size_t i = 0; i < analysis_results_.size(); ++i) {
                active_stddevs[i] = analysis_results_[i].get_stddev();
            }
        }

        // Deactivate the core before the next iteration.
        stress_tester->set_thread_busy_state(stressed_core_id, false);


        // --- Step 3: Calculate Correlation and Commit Results ---
        // This is the critical update step. We lock the real data structure and
        // modify it directly. This guarantees the GUI thread will see the change.
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            spdlog::info("Analysis: Committing results for core {}.", stressed_core_id);
            for (size_t i = 0; i < analysis_results_.size(); ++i) {
                float baseline = baseline_stddevs[i];
                float active = active_stddevs[i];

                // Calculate the normalized increase in volatility.
                // A value of 0 means no change; 1 means it went from 0 to some activity.
                float correlation_strength = 0.0f;
                // Add a small epsilon to avoid division by zero for flat signals.
                float denominator = (active + baseline + 1e-9f);
                if (denominator > 0) {
                    // We only care about positive correlation (increase in activity).
                    correlation_strength = std::max(0.0f, (active - baseline) / denominator);
                }

                // Normalize to make weaker correlations more visible in the UI colors.
                correlation_strength = sqrtf(correlation_strength);

                // Use the helper to update the cell's correlation list.
                update_or_insert_correlation(analysis_results_[i], stressed_core_id, correlation_strength);
            }
        } // The lock is released here, making the results visible.
    }

    // --- Step 4: Cleanup ---
    // Re-enable all stress threads for manual user control.
    for (int i = 0; i < core_count; ++i) {
        stress_tester->set_thread_busy_state(i, true);
    }
    spdlog::info("Full correlation analysis complete.");
}

/**
 * @brief Helper to update the correlation list for a single cell.
 *
 * This function finds the correlation entry for the given core_id in the cell's
 * top_correlations list. If it exists, it updates the strength. If not, it adds
 * a new entry. It then sorts the list and truncates it to keep only the top results.
 *
 * @param stats The CellStats object for a single cell in the grid.
 * @param core_id The core that was just measured.
 * @param new_strength The newly calculated correlation strength.
 */
void AnalysisManager::update_or_insert_correlation(CellStats& stats, int core_id, float new_strength) {
    // Find if a correlation for this core already exists.
    auto it = std::find_if(stats.top_correlations.begin(), stats.top_correlations.end(),
        [core_id](const CoreCorrelationInfo& info) {
            return info.core_id == core_id;
        });

    if (it != stats.top_correlations.end()) {
        // It exists, so update it.
        it->correlation_strength = new_strength;
    } else {
        // It doesn't exist, so add it.
        stats.top_correlations.push_back({core_id, new_strength, 1.0f /* quality */});
    }

    // Sort the list by strength in descending order.
    std::sort(stats.top_correlations.begin(), stats.top_correlations.end(),
        [](const CoreCorrelationInfo& a, const CoreCorrelationInfo& b) {
            return a.correlation_strength > b.correlation_strength;
        });

    // Keep only the top 4 correlations.
    if (stats.top_correlations.size() > 4) {
        stats.top_correlations.resize(4);
    }
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