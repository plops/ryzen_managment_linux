#ifndef GUI_RUNNER_HPP
#define GUI_RUNNER_HPP

#include <atomic>
#include <memory> // For std::unique_ptr
#include <string>
#include <vector>
// Forward declarations to avoid including heavy headers
namespace popl {
template <typename T> class Value;
}
struct LocalMeasurement;
class PmTableReader;
class EyeCapturer;
class EyeDiagramStorage;
struct GLFWwindow;

/**
 * @brief Manages the entire lifecycle of the ImGui-based GUI.
 *
 * This class encapsulates window creation, the render loop, experiment thread
 * management, and cleanup.
 */
class GuiRunner {
public:
  /**
   * @brief Constructs the GuiRunner and prepares for execution.
   *
   * @param rounds Number of experiment rounds.
   * @param num_hardware_threads Total number of system threads.
   * @param measurement_core The core to pin the measurement thread to.
   * @param period Worker task period.
   * @param duty_cycle Worker task duty cycle.
   * @param cycles Number of worker cycles per run.
   * @param measurement_view Buffer for raw measurement data.
   * @param pm_table_reader The PM Table reader instance.
   * @param capturer The eye diagram capturer instance.
   * @param eye_storage The eye diagram storage instance.
   * @param n_measurements Total number of sensors in the PM table.
   * @param interesting_index Vector of indices for sensors that are monitored.
   */
  GuiRunner(int rounds, int num_hardware_threads, int measurement_core,
            int period, int duty_cycle, int cycles,
            std::vector<LocalMeasurement> &measurement_view,
            PmTableReader &pm_table_reader, size_t n_measurements,
            const std::vector<int> &interesting_index);

  ~GuiRunner();

  // Prevent copying and moving
  GuiRunner(const GuiRunner &) = delete;
  GuiRunner &operator=(const GuiRunner &) = delete;

  /**
   * @brief Initializes the GUI and starts the main render loop.
   * @return 0 on success, non-zero on failure.
   */
  int run();

private:
  // Experiment parameters
  int rounds_;
  int num_hardware_threads_;
  int measurement_core_;
  int period_;
  int duty_cycle_;
  int cycles_;

  // Data structures
  std::vector<LocalMeasurement> &measurement_view_;
  PmTableReader &pm_table_reader_;
  // EyeCapturer& capturer_; // No longer needed as a member
  // EyeDiagramStorage& eye_storage_; // Now managed internally

  // GUI parameters
  size_t n_measurements_;
  const std::vector<int> &interesting_index_;

  // Internal state
  GLFWwindow *window_ = nullptr;

  // --- NEW: Double-buffering for lock-free data exchange ---
  std::unique_ptr<EyeDiagramStorage> storage_buffer_a_;
  std::unique_ptr<EyeDiagramStorage> storage_buffer_b_;
  /**
   * @brief Atomically managed pointer to the "read" buffer for the GUI.
   * The experiment thread writes to the other buffer and then swaps them.
   */
  std::atomic<EyeDiagramStorage *> gui_read_buffer_;

  // Private helper to launch the experiment thread
  void run_experiment_thread();

  // --- NEW: Manual mode control ---
  std::atomic<bool> manual_mode_{false};
  std::atomic<int> manual_core_to_test_{1};
  std::atomic<bool> terminate_thread_{false};
};

#endif // GUI_RUNNER_HPP
