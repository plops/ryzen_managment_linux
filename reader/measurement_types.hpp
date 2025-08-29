#pragma once
#include <vector>
#include <chrono>
#include <cstdint>

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

// Represents a single sample from the measurement thread
struct MeasurementSample {
    explicit MeasurementSample(size_t n_measurements) : measurements(n_measurements) {}
    TimePoint timestamp{};
    int worker_state{0}; // 0 for waiting, 1 for busy
    std::vector<float> measurements;
    std::vector<uint64_t> eye_off0;
    std::vector<uint64_t> eye_on;
    std::vector<uint64_t> eye_off1;
};

// Represents a state transition event from the worker thread
struct WorkerTransition {
    TimePoint timestamp;
    int new_state{}; // 0 for waiting, 1 for busy
};

