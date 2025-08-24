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
 * @brief Final version of the analysis function.
 *
 * This version makes the correlation results persistent and cumulative throughout a
 * single analysis run.
 *
 * Fix:
 * A new initialization step has been added at the beginning. Before the core-by-core
 * analysis begins, it clears all correlation data from the previous run. This
 * ensures that as each core's new results are calculated, they are added to a clean
 * slate and will persist in the UI until the user clicks "Run Analysis" again.
 *
 * @brief MODIFIED: Real-time Correlation Update.
 *
 * The active measurement phase has been changed from a single long sleep to a loop.
 * This loop periodically recalculates the correlation based on the data gathered so far
 * and updates the shared analysis_results_. This allows the GUI thread to render
 * the color changes in real-time as the measurement progresses.
 */
void AnalysisManager::run_correlation_analysis(StressTester* stress_tester) {
    spdlog::info("Starting REAL-TIME correlation analysis...");
    const int core_count = stress_tester->get_core_count();
    const auto baseline_duration = std::chrono::milliseconds(1500);
    const auto active_duration = std::chrono::seconds(2);
    // How often to update the UI during the active measurement phase.
    const auto update_interval = std::chrono::milliseconds(1000 / 60); // Approx 60 Hz


    // --- NEW: Initialization Step ---
    // Before starting the measurements, clear all correlation results from any previous run.
    // This ensures that the table starts empty and the new results are cumulative.
    {
        std::lock_guard<std::mutex> lock(results_mutex_);
        spdlog::info("Clearing all previous correlation data.");
        for (auto& result : analysis_results_) {
            result.top_correlations.clear();
        }
    }
    // The UI will now show a blank (uncolored) grid, ready for the new results.


    // Ensure all stress threads are initially off before we begin.
    for (int i = 0; i < core_count; ++i) {
        stress_tester->set_thread_busy_state(i, false);
    }

    // Loop through each core to stress it individually.
    for (int stressed_core_id = 0; stressed_core_id < core_count; ++stressed_core_id) {
        spdlog::info("Analysis: Measuring core {}...", stressed_core_id);

        // --- Step 1: Baseline Measurement (Core Idle) ---
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            for (auto& result : analysis_results_) result.history.clear();
        }
        std::this_thread::sleep_for(baseline_duration);

        std::vector<float> baseline_stddevs(analysis_results_.size());
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            for (size_t i = 0; i < analysis_results_.size(); ++i) {
                baseline_stddevs[i] = analysis_results_[i].get_stddev();
            }
        }

        // --- Step 2: Active Measurement (Core Stressed) ---
        stress_tester->set_thread_busy_state(stressed_core_id, true);
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            for (auto& result : analysis_results_) result.history.clear();
        }

        // --- REAL-TIME UPDATE LOOP ---
        // Instead of one long sleep, we loop and update the results frequently.
        auto measurement_start_time = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - measurement_start_time < active_duration) {
            // Wait for a short interval before the next update.
            std::this_thread::sleep_for(update_interval);

            // Lock data, calculate correlation based on samples gathered *so far*, and update.
            std::lock_guard<std::mutex> lock(results_mutex_);
            for (size_t i = 0; i < analysis_results_.size(); ++i) {
                // Get stddev from the history that has been accumulating.
                float active = analysis_results_[i].get_stddev();
                float baseline = baseline_stddevs[i];
                float correlation_strength = 0.0f;
                float denominator = (active + baseline + 1e-9f);
                if (denominator > 0) {
                    correlation_strength = std::max(0.0f, (active - baseline) / denominator);
                }
                correlation_strength = sqrtf(correlation_strength);

                // This function updates the list that the GUI thread is reading from.
                update_or_insert_correlation(analysis_results_[i], stressed_core_id, correlation_strength);
            }
        }

        stress_tester->set_thread_busy_state(stressed_core_id, false);

        // --- Step 3 is now integrated into the loop above ---
        spdlog::info("Analysis: Finished real-time measurement for core {}.", stressed_core_id);

    }

    // --- Step 4: Cleanup ---
    for (int i = 0; i < core_count; ++i) {
        stress_tester->set_thread_busy_state(i, true);
    }
    spdlog::info("Full correlation analysis complete. All results are now displayed.");
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