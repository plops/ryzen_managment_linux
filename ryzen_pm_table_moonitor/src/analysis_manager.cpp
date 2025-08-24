#include "analysis_manager.hpp"
#include <spdlog/spdlog.h>

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
    // Wait until queue is not empty or thread is stopping
    queue_cv_.wait(lock, [this] { return !data_queue_.empty() || !is_running_; });

    if (!is_running_) return;

    // Process all items currently in the queue
    while (!data_queue_.empty()) {
        std::vector<float> raw_data = std::move(data_queue_.front());
        data_queue_.pop();

        lock.unlock(); // Unlock while processing

        {
            std::lock_guard<std::mutex> results_lock(results_mutex_);
            if (analysis_results_.size() != raw_data.size()) {
                analysis_results_.assign(raw_data.size(), CellStats());
            }

            for (size_t i = 0; i < raw_data.size(); ++i) {
                analysis_results_[i].add_sample(raw_data[i]);
            }
        }

        lock.lock(); // Re-lock to check queue condition
    }
}

void AnalysisManager::run_correlation_analysis() {
    if (!stress_tester_) return;

    spdlog::debug("Running correlation analysis...");
    std::lock_guard<std::mutex> lock(results_mutex_);
    if (analysis_results_.empty() || analysis_results_[0].history.size() < CellStats::HISTORY_SIZE) {
        spdlog::warn("Analysis skipped: not enough historical data yet.");
        return;
    }

    const auto& periods = stress_tester_->get_periods();
    double sample_rate = 1000.0; // We are processing at the full 1kHz rate now!
    double max_magnitude_overall = 0;

    std::vector<double> magnitudes(analysis_results_.size());

    // First pass: Find max magnitude across all cells for normalization
    for (size_t i = 0; i < analysis_results_.size(); ++i) {
        double max_mag_for_cell = 0;
        for (int core_id = 0; core_id < stress_tester_->get_core_count(); ++core_id) {
            double freq = 1000.0 / periods[core_id].count();
            double mag = goertzel_magnitude(analysis_results_[i].history, freq, sample_rate);
            if (mag > max_mag_for_cell) max_mag_for_cell = mag;
        }
        magnitudes[i] = max_mag_for_cell;
        if (max_mag_for_cell > max_magnitude_overall) max_magnitude_overall = max_mag_for_cell;
    }

    // Second pass: Assign dominant core and normalized strength
    for (size_t i = 0; i < analysis_results_.size(); ++i) {
        analysis_results_[i].correlation_strength = (max_magnitude_overall > 0) ? (std::sqrt(magnitudes[i]) / std::sqrt(max_magnitude_overall)) : 0.0;

        double max_mag_for_cell = 0;
        int dominant_core = -1;
        for (int core_id = 0; core_id < stress_tester_->get_core_count(); ++core_id) {
            double freq = 1000.0 / periods[core_id].count();
            double mag = goertzel_magnitude(analysis_results_[i].history, freq, sample_rate);
            if (mag > max_mag_for_cell) {
                max_mag_for_cell = mag;
                dominant_core = core_id;
            }
        }
        analysis_results_[i].dominant_core_id = dominant_core;
    }
    spdlog::info("Correlation analysis complete.");
}