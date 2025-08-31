#include "eye_diagram.hpp"
#include <algorithm>

/**
 * @brief Construct EyeDiagramStorage and allocate with default window sizes.
 */
EyeDiagramStorage::EyeDiagramStorage(std::span<const int> interesting_indices, size_t reserve_per_bin)
    : EyeDiagramStorage(interesting_indices, reserve_per_bin, DEFAULT_WINDOW_BEFORE_MS, DEFAULT_WINDOW_AFTER_MS) { }

/**
 * @brief Construct EyeDiagramStorage, set custom window parameters, and allocate.
 *
 * This prevents reallocations during capture if reserve_per_bin is chosen appropriately.
 * It also stores the mapping from internal storage index to original sensor index.
 */
EyeDiagramStorage::EyeDiagramStorage(std::span<const int> interesting_indices, size_t reserve_per_bin, int window_before_ms, int window_after_ms)
{
    this->window_before_ms = window_before_ms;
    this->window_after_ms = window_after_ms;
    this->zero_offset_bins = window_before_ms;
    this->num_bins = window_before_ms + window_after_ms;

    // Store the mapping from our internal index to the original sensor index
    this->original_sensor_indices.assign(interesting_indices.begin(), interesting_indices.end());

    size_t n_sensors = original_sensor_indices.size();
    // Bins are now indexed [sensor_index][bin_index] for better memory locality,
    // as we typically process all bins for one sensor at a time during analysis.
    bins.resize(n_sensors);
    for (auto &sensor_bins : bins) {
        sensor_bins.resize(num_bins);
        for (auto &bin : sensor_bins) {
            bin.reserve(reserve_per_bin);
        }
    }
}

/** @brief Clear all stored samples but keep reserved capacity. */
void EyeDiagramStorage::clear() {
    event_count = 0;
    for (auto &sensor_bins : bins) {
        for (auto &vec : sensor_bins) {
            vec.clear(); // keep capacity
        }
    }
}
