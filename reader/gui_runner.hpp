#ifndef GUI_RUNNER_HPP
#define GUI_RUNNER_HPP

#include "shared_data_types.hpp"
#include <atomic>
#include <memory>
#include <vector>

// Forward declarations
class PmTableReader;
struct GLFWwindow;

class GuiRunner {
public:
  GuiRunner(int num_hardware_threads, int measurement_core, int period,
            int duty_cycle, int cycles, PmTableReader &pm_table_reader,
            size_t n_measurements, const std::vector<int> &interesting_index);

  ~GuiRunner();

  GuiRunner(const GuiRunner &) = delete;
  GuiRunner &operator=(const GuiRunner &) = delete;

  int run();

private:
  // Thread Functions
  void run_processing_thread();
  void run_worker_thread() const;

  // Experiment parameters
  int num_hardware_threads_;
  int measurement_core_;
  int period_ms_;
  int duty_cycle_percent_;
  int num_cycles_;
  size_t n_measurements_;
  const std::vector<int> &interesting_index_;

  // System resources
  PmTableReader &pm_table_reader_;
  GLFWwindow *window_ = nullptr;

  // Thread communication and data structures
  folly::ProducerConsumerQueue<RawSample> spsc_queue_;
  CommandQueue command_queue_;

  std::vector<std::unique_ptr<DisplayData>> display_data_a_; // Write buffer A
  std::vector<std::unique_ptr<DisplayData>> display_data_b_; // Write buffer B
  std::vector<std::atomic<DisplayData *>>
      gui_display_pointers_; // Pointers for GUI to read

  // Thread control
  std::atomic<bool> manual_mode_{true};
  std::atomic<int> manual_core_to_test_{1};
  std::atomic<bool> terminate_threads_{false};
  std::atomic<int> max_accumulations_{30}; // Default value
};

#endif // GUI_RUNNER_HPP