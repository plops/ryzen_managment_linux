#pragma once
#include "eye_diagram.hpp"
#include <vector>
#include <chrono>
#include <span>
#include <unordered_map>

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
     * @brief Construct an EyeCapturer bound to a pre-configured EyeDiagramStorage.
     * The capturer infers which sensors to track from the storage object.
     * @param storage Storage instance that will receive binned samples.
     */
    EyeCapturer(EyeDiagramStorage &storage);

    /**
     * @brief Process one measurement sample.
     *
     * The function detects rising edges based on worker_state and bins each sensor
     * value from measurements into storage relative to the last rising edge timestamp.
     *
     * @param timestamp Sample timestamp.
     * @param worker_state Current worker state (0 or 1).
     * @param measurements View over floating-point sensor values (no copy).
     */
    bool process_sample(const TimePoint &timestamp, int worker_state, std::span<const float> measurements);

    /**
     * @brief Re-points the capturer to a new storage object.
     * Used in the double-buffering scheme to swap write buffers.
     * @param storage The new EyeDiagramStorage to write to.
     */
    void set_storage(EyeDiagramStorage &storage);

private:
    EyeDiagramStorage &storage_;
    /**
     * @brief Fast lookup from original sensor index to internal storage index.
     * This is built once at construction from the storage's `original_sensor_indices`.
     */
    std::unordered_map<int, size_t> sensor_to_storage_idx_;
    int last_worker_state_{0};
    TimePoint last_rise_time_;
    enum class State { IDLE, CAPTURING } state_{State::IDLE};
};
