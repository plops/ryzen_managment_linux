#pragma once

#include "measurement_types.hpp" // For TimePoint
#include "stats_utils.hpp"       // For calculate_trimmed_mean

#include <folly/ProducerConsumerQueue.h>

#include <array>
#include <atomic>
#include <mutex>
#include <queue>
#include <variant>
#include <vector>

// Define a safe upper bound for your system's pm_table size in floats.
// If the table is max 8192 bytes, this would be 2048 floats. Adjust as needed.
constexpr size_t PM_TABLE_MAX_FLOATS = 2048;

/**
 * @struct RawSample
 * @brief The data packet produced by the Measurement Thread.
 */
struct RawSample {
  TimePoint timestamp{};
  int worker_state{};
  std::array<float, PM_TABLE_MAX_FLOATS> measurements;
  size_t num_measurements{};
};

/**
 * @struct DisplayData
 * @brief Render-ready data produced by the Processing Thread for one sensor.
 */
struct DisplayData {
  // Core plot data
  std::vector<float> x_data;      // Time in ms relative to trigger
  std::vector<float> y_data_mean; // Trimmed mean
  std::vector<float> y_data_max;  // Max envelope
  std::vector<float> y_data_min;  // Min envelope

  // Metadata
  int original_sensor_index = -1;
  size_t accumulation_count = 0;
  int window_before_ms = 50; // Use a non-zero default
  int window_after_ms = 150; // Use a non-zero default

  void clear() {
    x_data.clear();
    y_data_mean.clear();
    y_data_max.clear();
    y_data_min.clear();
    accumulation_count = 0;
  }
};

// --- Command queue for GUI -> Processing thread communication ---

struct ChangeCoreCmd {
  int new_core_id;
};
struct ChangeAccumulationsCmd {
  int new_count;
};

using GuiCommand = std::variant<ChangeCoreCmd, ChangeAccumulationsCmd>;

class CommandQueue {
public:
  void push(const GuiCommand &cmd) {
    std::lock_guard lock(mutex_);
    queue_.push(cmd);
  }

  bool try_pop(GuiCommand &cmd) {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
      return false;
    }
    cmd = queue_.front();
    queue_.pop();
    return true;
  }

private:
  std::queue<GuiCommand> queue_;
  std::mutex mutex_;
};