#include "analysis_manager.hpp"
#include <spdlog/spdlog.h>
#include <numeric>
#include <algorithm> // For std::min

void AnalysisManager::analysis_loop() {
    spdlog::info("Analysis thread started.");
    while (is_running_) {
        process_queue();

        if (reset_stats_flag_) {
            std::lock_guard<std::mutex> lock(results_mutex_);
            for (auto& stats : analysis_results_) {
                stats.reset();
            }
            reset_stats_flag_ = false;
        }

        if (run_analysis_flag_) {
            run_correlation_analysis();
            run_analysis_flag_ = false; // Reset flag
        }
    }
    spdlog::info("Analysis thread stopped.");
}

void AnalysisManager::process_queue() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [this] { return !data_queue_.empty() || !is_running_; });

    if (!is_running_) return;

    while (!data_queue_.empty()) {
        TimestampedData current_data = std::move(data_queue_.front());
        data_queue_.pop();
        lock.unlock();

        {
            std::lock_guard<std::mutex> results_lock(results_mutex_);
            if (analysis_results_.size() != current_data.data.size()) {
                analysis_results_.assign(current_data.data.size(), CellStats());
            }

            for (size_t i = 0; i < current_data.data.size(); ++i) {
                analysis_results_[i].add_sample(current_data.data[i], current_data.timestamp_ns);
            }
        }
        lock.lock();
    }
}


void AnalysisManager::run_correlation_analysis() {
    if (!stress_tester_ || !stress_tester_->is_running()) {
        spdlog::warn("Analysis skipped: stress tester is not running.");
        return;
    }

    spdlog::debug("Running correlation analysis...");
    std::lock_guard<std::mutex> lock(results_mutex_);
    if (analysis_results_.empty() || analysis_results_[0].history.empty()) {
        spdlog::warn("Analysis skipped: not enough historical data yet.");
        return;
    }

    const auto& periods = stress_tester_->get_periods();
    const auto stress_start_time = stress_tester_->get_start_time();
    const long long stress_start_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stress_start_time.time_since_epoch()).count();

    for (size_t i = 0; i < analysis_results_.size(); ++i) {
        auto& cell = analysis_results_[i];
        int best_core_id = -1;
        float max_abs_diff = -1.0f;

        // Find the core with the maximum absolute mean difference
        for (int core_id = 0; core_id < stress_tester_->get_core_count(); ++core_id) {
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

            // Skip if we don't have samples for both states to compare
            if (on_state_samples.empty() || off_state_samples.empty()) {
                continue;
            }

            double on_mean = std::accumulate(on_state_samples.begin(), on_state_samples.end(), 0.0) / on_state_samples.size();
            double off_mean = std::accumulate(off_state_samples.begin(), off_state_samples.end(), 0.0) / off_state_samples.size();

            float diff = on_mean - off_mean;
            if (std::abs(diff) > max_abs_diff) {
                max_abs_diff = std::abs(diff);
                best_core_id = core_id;
            }
        }

        cell.dominant_core_id = best_core_id;

        // --- NEW: Normalization Logic ---
        // If a dominant core was found, normalize its score.
        if (best_core_id != -1) {
            // Calculate the dynamic range of the signal from its history.
            double range = cell.max_val - cell.min_val;

            // Avoid division by zero if the signal was flat.
            if (range > 1e-6) {
                double normalized_strength = max_abs_diff / range;
                // Clamp the value to the [0, 1] range to handle any floating point oddities.
                cell.correlation_strength = std::min(1.0, normalized_strength);
            } else {
                cell.correlation_strength = 0.0f; // No range, so no correlation.
            }
        } else {
            cell.correlation_strength = 0.0f; // No dominant core found.
        }
    }
    spdlog::info("Correlation analysis complete.");
}