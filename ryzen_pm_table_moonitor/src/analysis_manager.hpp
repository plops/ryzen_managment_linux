#pragma once

#include "analysis.hpp" // We still need CellStats and goertzel_magnitude
#include "stress_tester.hpp"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

class AnalysisManager {
public:
    AnalysisManager() = default;
    ~AnalysisManager() {
        stop();
    }

    void start(const StressTester* stress_tester) {
        if (is_running_) return;
        is_running_ = true;
        stress_tester_ = stress_tester;
        analysis_thread_ = std::thread(&AnalysisManager::analysis_loop, this);
    }

    void stop() {
        if (!is_running_) return;
        is_running_ = false;
        queue_cv_.notify_one();
        if (analysis_thread_.joinable()) {
            analysis_thread_.join();
        }
    }

    // Called by PMTableReader thread (Producer)
    void submit_raw_data(std::vector<float>&& data) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            data_queue_.push(std::move(data));
        }
        queue_cv_.notify_one();
    }

    // Called by GUI thread to get the latest results
    std::vector<CellStats> get_analysis_results() {
        std::lock_guard<std::mutex> lock(results_mutex_);
        return analysis_results_;
    }

    void trigger_analysis() {
        run_analysis_flag_.store(true);
    }

    void reset_stats() {
        // Pass reset command to analysis thread
        reset_stats_flag_.store(true);
    }

private:
    void analysis_loop();
    void process_queue();
    void run_correlation_analysis();

    // Producer/Consumer Queue for raw data
    std::queue<std::vector<float>> data_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // Analysis results
    std::vector<CellStats> analysis_results_;
    std::mutex results_mutex_;

    // Thread management
    std::atomic<bool> is_running_{false};
    std::thread analysis_thread_;
    std::atomic<bool> run_analysis_flag_{false};
    std::atomic<bool> reset_stats_flag_{false};
    const StressTester* stress_tester_ = nullptr;
};