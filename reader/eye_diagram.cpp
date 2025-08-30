#include "eye_diagram.hpp"

/**
 * @brief Construct EyeDiagramStorage and reserve capacity for all bins and sensors.
 *
 * This prevents reallocations during capture if reserve_per_bin is chosen appropriately.
 */
EyeDiagramStorage::EyeDiagramStorage(size_t n_sensors, size_t reserve_per_bin) {
    bins.resize(NUM_BINS);
    for (auto &bin : bins) {
        bin.resize(n_sensors);
        for (auto &vec : bin) {
            vec.reserve(reserve_per_bin);
        }
    }
}

/** @brief Clear all stored samples but keep reserved capacity. */
void EyeDiagramStorage::clear() {
    event_count = 0;
    for (auto &bin : bins) {
        for (auto &vec : bin) {
            vec.clear(); // keep capacity
        }
    }
}

/**
 * @brief Store a single sample into the requested bin and sensor vector.
 *
 * Bounds-checked (no exceptions) and silently ignored if indexes are invalid.
 */
void EyeDiagramStorage::add_sample(int bin_index, size_t sensor_index, float value) {
    if (bin_index < 0 || bin_index >= NUM_BINS) return;
    if (sensor_index >= bins[bin_index].size()) return;
    bins[bin_index][sensor_index].push_back(value); // The push_back shouldn't allocate. Vector is reserved
}
