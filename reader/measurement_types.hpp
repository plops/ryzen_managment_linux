#pragma once
#include <vector>
#include <chrono>
#include <cstdint>

/**
 * @file measurement_types.hpp
 * @brief Fundamental data types used in the measurement harness.
 */

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

/**
 * @struct MeasurementSample
 * @brief Represents a single timestamped sample of all floating-point sensors.
 *
 * - measurements: vector<float> sized according to pm_table contents.
 * - eye_off0/on/off1: optional histogram counters (kept for backward compatibility).
 */
struct MeasurementSample {
    explicit MeasurementSample(size_t n_measurements) : measurements(n_measurements) {}
    TimePoint timestamp{};
    int worker_state{0}; // 0 for waiting, 1 for busy
    std::vector<float> measurements;
};

/**
 * @struct WorkerTransition
 * @brief Timestamped state transition recorded when the worker toggles state.
 */
struct WorkerTransition {
    TimePoint timestamp;
    int new_state{}; // 0 for waiting, 1 for busy
};
