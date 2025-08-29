#include "eye_capturer.hpp"

EyeCapturer::EyeCapturer(EyeDiagramStorage &storage, size_t n_sensors)
    : storage_(storage), n_sensors_(n_sensors) { }

void EyeCapturer::process_sample(const TimePoint &timestamp, int worker_state, const std::vector<float> &measurements) {
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
        long long bin_index = time_delta_ms + EyeDiagramStorage::ZERO_OFFSET_BINS;

        if (bin_index >= 0 && bin_index < EyeDiagramStorage::NUM_BINS) {
            // Bin each sensor's value for this timestamp
            for (size_t si = 0; si < n_sensors_ && si < measurements.size(); ++si) {
                storage_.add_sample(static_cast<int>(bin_index), si, measurements[si]);
            }
        } else if (bin_index >= EyeDiagramStorage::NUM_BINS) {
            // End of capture window
            state_ = State::IDLE;
        }
    }
}
