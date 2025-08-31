#pragma once
#include <vector>
#include <cstddef>
#include <span>

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
 * This class now also stores the original indices of the "interesting" sensors
 * it was configured to track, simplifying analysis.
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

    // bins[storage_index][bin_index] => vector<float> of values for that bin & sensor
    // Note the swapped index order for better memory access patterns.
    std::vector<std::vector<std::vector<float>>> bins;
    size_t event_count{0};

    /**
     * @brief The original indices of the sensors being stored.
     * The index in this vector is the `storage_index` used internally.
     * The value is the original sensor index from the pm_table.
     * Example: `original_sensor_indices[0]` might be `17`.
     */
    std::vector<int> original_sensor_indices;

    /** @brief Default constructor for an empty/uninitialized storage. */
    EyeDiagramStorage() = default;

    /**
     * @brief Construct and allocate storage with default window sizes.
     * @param interesting_indices The original indices of sensors to track.
     * @param reserve_per_bin Expected number of samples per sensor per bin.
     */
    EyeDiagramStorage(std::span<const int> interesting_indices, size_t reserve_per_bin);

    /**
     * @brief Construct and allocate storage with custom window sizes.
     * @param interesting_indices The original indices of sensors to track.
     * @param reserve_per_bin Expected number of samples per sensor per bin.
     * @param window_before_ms Look-back window in milliseconds (defines zero offset).
     * @param window_after_ms Look-forward window in milliseconds.
     */
    EyeDiagramStorage(std::span<const int> interesting_indices, size_t reserve_per_bin, int window_before_ms, int window_after_ms);

    /** @brief Clear stored samples but keep reserved capacity. */
    void clear(); // clears stored samples but keeps allocation
};
