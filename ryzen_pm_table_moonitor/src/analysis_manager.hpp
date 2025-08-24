// start of analysis_manager.hpp
#pragma once

#include "analysis.hpp"
#include "stress_tester.hpp"
#include <vector>
#include <mutex>
#include <atomic> // Keep atomic flags for GUI-triggered actions

// Struct to hold a vector of raw data with its capture timestamp
struct TimestampedData {
    long long timestamp_ns;
    std::vector<float> data;
};

class AnalysisManager {
public:
    AnalysisManager() = default;

    // The GUI thread calls this to get the latest results for rendering.
    std::vector<CellStats> get_analysis_results();

    // The pipeline will call this for each new data packet.
    // CHANGE: Take by const reference to read from the shared buffer without moving.
    void process_data_packet(const TimestampedData& data);

    // This will be called by a task submitted from the GUI.
    void run_correlation_analysis(const StressTester* stress_tester);

    // This will be called by a task submitted from the GUI.
    void reset_stats();

private:
    // Analysis results remain, protected by a mutex for GUI access.
    std::vector<CellStats> analysis_results_;
    std::mutex results_mutex_;
};