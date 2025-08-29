#pragma once
#include "eye_diagram.hpp"
#include <vector>
#include <chrono>

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

class EyeCapturer {
public:
    EyeCapturer(EyeDiagramStorage &storage, size_t n_sensors);

    // Process a single sample (timestamp + worker_state + full measurement vector).
    // This method updates internal state and bins values into storage.
    void process_sample(const TimePoint &timestamp, int worker_state, const std::vector<float> &measurements);

private:
    EyeDiagramStorage &storage_;
    size_t n_sensors_;
    int last_worker_state_{0};
    TimePoint last_rise_time_;
    enum class State { IDLE, CAPTURING } state_{State::IDLE};
};

