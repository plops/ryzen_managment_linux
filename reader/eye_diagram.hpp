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
 * - num_bins is (window_before_ms + window_after_ms).
 *
 * Vectors are reserved at construction to avoid reallocations during a run.
 */
struct EyeDiagramStorage {
    // Default compile-time fallback values (can be overridden at runtime)
    static const int DEFAULT_WINDOW_BEFORE_MS = 50;
    static const int DEFAULT_WINDOW_AFTER_MS = 150;

    // Runtime configuration (set at construction)
    int window_before_ms{DEFAULT_WINDOW_BEFORE_MS};
    int window_after_ms{DEFAULT_WINDOW_AFTER_MS};
    int num_bins{DEFAULT_WINDOW_BEFORE_MS + DEFAULT_WINDOW_AFTER_MS};
    int zero_offset_bins{DEFAULT_WINDOW_BEFORE_MS};

    // bins[bin_index][sensor_index] => vector<float> of values for that bin & sensor
    std::vector<std::vector<std::vector<float>>> bins;
    size_t event_count{0};

    /** @brief Default constructor. */
    EyeDiagramStorage() = default;

    /**
     * @brief Construct and pre-allocate inner vectors.
     * @param n_sensors Number of floating-point sensors to track.
     * @param reserve_per_bin Expected number of samples per sensor per bin.
     * @param window_before_ms Look-back window in milliseconds (defines zero offset).
     * @param window_after_ms Look-forward window in milliseconds.
     */
    EyeDiagramStorage(size_t n_sensors,
                      size_t reserve_per_bin,
                      int window_before_ms = DEFAULT_WINDOW_BEFORE_MS,
                      int window_after_ms = DEFAULT_WINDOW_AFTER_MS);

    /** @brief Clear stored samples but keep reserved capacity. */
    void clear(); // clears stored samples but keeps allocation

    /**
     * @brief Add a sample value for a given bin and sensor.
     * @param bin_index Index of the time bin (0..num_bins-1).
     * @param sensor_index Index of the sensor.
     * @param value Floating-point value to store.
     */
    void add_sample(int bin_index, size_t sensor_index, float value);
};
