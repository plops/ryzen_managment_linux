#pragma once
#include <vector>
#include <cstddef>

struct EyeDiagramStorage {
    // Configuration
    static constexpr int WINDOW_BEFORE_MS = 50;   // Look back
    static constexpr int WINDOW_AFTER_MS = 150;   // Look forward
    static constexpr int NUM_BINS = WINDOW_BEFORE_MS + WINDOW_AFTER_MS;
    static constexpr int ZERO_OFFSET_BINS = WINDOW_BEFORE_MS;

    // bins[bin_index][sensor_index] => vector<float> of values for that bin & sensor
    std::vector<std::vector<std::vector<float>>> bins;
    size_t event_count{0};

    EyeDiagramStorage() = default;
    // Construct and pre-allocate inner vectors
    EyeDiagramStorage(size_t n_sensors, size_t reserve_per_bin);

    void clear(); // clears stored samples but keeps allocation
    void add_sample(int bin_index, size_t sensor_index, float value);
};

