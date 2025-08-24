#pragma once

#include <vector>
#include <cmath>
#include <limits>
#include <numeric>

// A Goertzel algorithm implementation to efficiently detect the strength of our known frequencies in the data streams. The Goertzel algorithm is much more efficient than a full FFT when you only care about a few specific frequencies.

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
    std::vector<float> history;
    int dominant_core_id = -1;
    float correlation_strength = 0.0f; // Normalized strength [0, 1]

    CellStats() {
        history.reserve(HISTORY_SIZE);
    }

    void add_sample(float value) {
        current_val = value;
        if (value < min_val) min_val = value;
        if (value > max_val) max_val = value;

        // Welford's algorithm for running mean and std dev
        count++;
        double delta = value - mean;
        mean += delta / count;
        double delta2 = value - mean;
        m2 += delta * delta2;

        // Add to history for frequency analysis
        if (history.size() < HISTORY_SIZE) {
            history.push_back(value);
        } else {
            // Ring buffer behavior
            history[count % HISTORY_SIZE] = value;
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
    }
};


// Goertzel algorithm to detect the magnitude of a specific frequency in a signal
inline double goertzel_magnitude(const std::vector<float>& data, double target_frequency, double sample_rate) {
    if (data.empty()) return 0.0;

    int k = static_cast<int>(0.5 + (data.size() * target_frequency) / sample_rate);
    double omega = (2.0 * M_PI * k) / data.size();
    double cosine = cos(omega);
    double sine = sin(omega);
    double coeff = 2.0 * cosine;

    double q0 = 0, q1 = 0, q2 = 0;

    for (float sample : data) {
        q0 = coeff * q1 - q2 + sample;
        q2 = q1;
        q1 = q0;
    }

    // Classic Goertzel result
    double real = (q1 - q2 * cosine);
    double imag = (q2 * sine);

    // Returns magnitude squared
    return real * real + imag * imag;
}