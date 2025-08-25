#pragma once

#include <vector>
#include <cmath>
#include <limits>
#include <numeric>
#include <deque>
#include <algorithm> // Required for std::sort

// A sample with its value and the precise time it was captured.
struct TimestampedSample {
    long long timestamp_ns;
    float value;
};

// NEW: A struct to hold the correlation result for a single core.
struct CoreCorrelationInfo {
    int core_id = -1;
    float correlation_strength = 0.0f;
    float correlation_quality = 0.0f;

    // Sort descending by strength, making it easy to find the top contributors.
    bool operator<(const CoreCorrelationInfo& other) const {
        return correlation_strength > other.correlation_strength;
    }
};

// Holds the analysis results for a single float from the PM table
struct CellStats {
    float min_val = std::numeric_limits<float>::max();
    float max_val = std::numeric_limits<float>::lowest();
    float current_val = 0.0f;
    double mean = 0.0;
    double m2 = 0.0; // Sum of squares of differences from the mean
    int count = 0;

    // For correlation analysis
    static constexpr int HISTORY_SIZE = 2'000; // Increased to 400 for hover graph
    std::deque<TimestampedSample> history; // Use deque for efficient front removal

    // CHANGED: Replaced single dominant core with a vector for the top correlated cores.
    std::vector<CoreCorrelationInfo> top_correlations;


    void add_sample(float value, long long timestamp_ns) {
        current_val = value;
        if (value < min_val) min_val = value;
        if (value > max_val) max_val = value;

        // Welford's algorithm for running mean and std dev
        count++;
        double delta = value - mean;
        mean += delta / count;
        double delta2 = value - mean;
        m2 += delta * delta2;

        // Add to history for correlation analysis
        history.push_back({timestamp_ns, value});
        if (history.size() > HISTORY_SIZE) {
            history.pop_front();
        }
    }

    [[nodiscard]] float get_stddev() const {
        if (count < 2) return 0.0f;
        return std::sqrtf(m2 / (count - 1));
    }

    void reset() {
        min_val = std::numeric_limits<float>::max();
        max_val = std::numeric_limits<float>::lowest();
        mean = 0.0;
        m2 = 0.0;
        count = 0;
        history.clear();
        // CHANGED: Clear the new vector on reset.
        top_correlations.clear();
    }
};