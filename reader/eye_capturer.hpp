#pragma once
#include "eye_diagram.hpp"
#include <vector>
#include <chrono>

/**
 * @file eye_capturer.hpp
 * @brief State machine that detects worker rising edges and fills EyeDiagramStorage.
 */

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

/**
 * @class EyeCapturer
 * @brief Encapsulates the capture state machine and regridding logic.
 *
 * On detecting a rising edge (worker state 0->1) capturing starts and subsequent
 * samples are binned relative to the rise time into EyeDiagramStorage until the
 * capture window end is reached.
 */
class EyeCapturer {
public:
    /**
     * @brief Construct an EyeCapturer bound to a pre-allocated EyeDiagramStorage.
     * @param storage Storage instance that will receive binned samples.
     * @param n_sensors Number of sensors (columns) expected in each measurement.
     */
    EyeCapturer(EyeDiagramStorage &storage, size_t n_sensors);

    /**
     * @brief Process one measurement sample.
     *
     * The function detects rising edges based on worker_state and bins each sensor
     * value from measurements into storage relative to the last rising edge timestamp.
     *
     * @param timestamp Sample timestamp.
     * @param worker_state Current worker state (0 or 1).
     * @param measurements Vector of floating-point sensor values.
     */
    void process_sample(const TimePoint &timestamp, int worker_state, const std::vector<float> &measurements);

private:
    EyeDiagramStorage &storage_;
    size_t n_sensors_;
    int last_worker_state_{0};
    TimePoint last_rise_time_;
    enum class State { IDLE, CAPTURING } state_{State::IDLE};
};
