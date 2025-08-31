#include "eye_capturer.hpp"
#include <span>               // optional but explicit
#include <mutex>              // For std::lock_guard

/**
 * @brief Construct an EyeCapturer.
 *
 * Stores a reference to EyeDiagramStorage and builds a reverse map for fast
 * lookup of a sensor's storage index from its original pm_table index.
 */
EyeCapturer::EyeCapturer(EyeDiagramStorage &storage)
    : storage_(storage) {
    // Build the reverse map for O(1) lookup of storage index from original sensor index
    for (size_t i = 0; i < storage.original_sensor_indices.size(); ++i) {
        int original_sensor_idx = storage.original_sensor_indices[i];
        sensor_to_storage_idx_[original_sensor_idx] = i;
    }
}

/**
 * @brief Re-points the capturer to a new storage object.
 */
void EyeCapturer::set_storage(EyeDiagramStorage &storage) {
    storage_ = storage;
    // The storage objects are identical in structure, so the map is still valid.
    // No need to rebuild sensor_to_storage_idx_.
}

/**
 * @brief Process a sample and bin sensor values relative to the most recent rising edge.
 *
 * - Detects a rising edge (0->1) and starts capture.
 * - Computes the millisecond bin index relative to the rising-edge timestamp.
 * - For each interesting sensor, finds its storage index and bins the value.
 *
 * @return true if a full eye has been captured (state is idle)
 */
bool EyeCapturer::process_sample(const TimePoint &timestamp, int worker_state, std::span<const float> measurements) {

    // Detect rising edge 0 -> 1
    if (worker_state == 1 && last_worker_state_ == 0) {
        state_ = State::CAPTURING;
        last_rise_time_ = timestamp;
        storage_.event_count++;
    }
    last_worker_state_ = worker_state;

    if (state_ == State::CAPTURING) {
        auto time_since_rise = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp - last_rise_time_);
        long long time_delta_ms = time_since_rise.count();

        // Use runtime-configurable zero offset and number of bins from storage_
        long long bin_index = time_delta_ms + static_cast<long long>(storage_.zero_offset_bins);

        if (bin_index >= 0 && bin_index < static_cast<long long>(storage_.num_bins)) {
            // For each sensor in the measurement, check if it's one we should store.
            for (size_t sensor_idx = 0; sensor_idx < measurements.size(); ++sensor_idx) {
                auto it = sensor_to_storage_idx_.find(sensor_idx);
                if (it != sensor_to_storage_idx_.end()) {
                    // This is an interesting sensor. Get its compact storage index.
                    size_t storage_idx = it->second;
                    // Add to storage: bins[sensor][bin]
                    storage_.bins[storage_idx][bin_index].push_back(measurements[sensor_idx]);
                }
            }
        } else if (bin_index >= static_cast<long long>(storage_.num_bins)) {
            // End of capture window
            state_ = State::IDLE;
        }
    }
    return state_ == State::IDLE;
}
