#pragma once

#include <vector>
#include <cmath>
#include <limits>
#include <numeric>
#include <deque>

// A sample with its value and the precise time it was captured.
struct TimestampedSample {
    long long timestamp_ns;
    float value;
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
    static constexpr int HISTORY_SIZE = 256;
    std::deque<TimestampedSample> history; // Use deque for efficient front removal
    int dominant_core_id = -1;
    float correlation_strength = 0.0f; // Represents the absolute difference between on-state and off-state means

    // NEW: A metric indicating our confidence in the correlation result.
    float correlation_quality = 0.0f;

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

    float get_stddev() const {
        if (count < 2) return 0.0f;
        return std::sqrt(m2 / (count - 1));
    }

    void reset() {
        min_val = std::numeric_limits<float>::max();
        max_val = std::numeric_limits<float>::lowest();
        mean = 0.0;
        m2 = 0.0;
        count = 0;
        history.clear();
        dominant_core_id = -1;
        correlation_strength = 0.0f;
        correlation_quality = 0.0f; // Reset the quality indicator as well
    }
};