#pragma once
#include <vector>
#include <cstddef>

/**
 * @file eye_diagram.hpp
 * @brief Storage and configuration for eye-diagram binning of sensor values.
 */

/**
 * @struct EyeDiagramStorage
 * @brief Holds per-bin, per-sensor vectors of floating-point samples.
 *
 * - bins[bin_index][sensor_index] is a vector<float> of samples falling into that time bin.
 * - NUM_BINS is (WINDOW_BEFORE_MS + WINDOW_AFTER_MS).
 *
 * Vectors are reserved at construction to avoid reallocations during a run.
 */
struct EyeDiagramStorage {
    // Configuration
    static constexpr int WINDOW_BEFORE_MS = 50;   // Look back
    static constexpr int WINDOW_AFTER_MS = 150;   // Look forward
    static constexpr int NUM_BINS = WINDOW_BEFORE_MS + WINDOW_AFTER_MS;
    static constexpr int ZERO_OFFSET_BINS = WINDOW_BEFORE_MS;

    // bins[bin_index][sensor_index] => vector<float> of values for that bin & sensor
    std::vector<std::vector<std::vector<float>>> bins;
    size_t event_count{0};

    /** @brief Default constructor. */
    EyeDiagramStorage() = default;
    /**
     * @brief Construct and pre-allocate inner vectors.
     * @param n_sensors Number of floating-point sensors to track.
     * @param reserve_per_bin Expected number of samples per sensor per bin.
     */
    EyeDiagramStorage(size_t n_sensors, size_t reserve_per_bin);

    /** @brief Clear stored samples but keep reserved capacity. */
    void clear(); // clears stored samples but keeps allocation

    /**
     * @brief Add a sample value for a given bin and sensor.
     * @param bin_index Index of the time bin (0..NUM_BINS-1).
     * @param sensor_index Index of the sensor.
     * @param value Floating-point value to store.
     */
    void add_sample(int bin_index, size_t sensor_index, float value);
};
