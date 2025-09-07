#include "gui_runner.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <numeric>
#include <thread>

#include "pm_table_reader.hpp"

// allow literals for time units
using namespace std::chrono_literals;

// Forward declarations from measure.cpp
void measurement_thread_func(int core_id,
                             folly::ProducerConsumerQueue<RawSample> &queue,
                             PmTableReader &pm_table_reader);
void worker_thread_func(int core_id, int period_ms, int duty_cycle_percent,
                        int num_cycles);

// Forward declaration from gui_render.cpp
void render_gui(
    const std::vector<std::atomic<DisplayData *>> &gui_display_pointers,
    int n_total_sensors, const std::vector<int> &interesting_indices,
    const std::string &experiment_status, CommandQueue &command_queue,
    std::atomic<bool> &manual_mode, std::atomic<int> &manual_core_to_test,
    int num_hardware_threads);

// Extern global flags from measure.cpp
extern std::atomic<bool> g_run_measurement;
extern std::atomic<int> g_worker_state;

GuiRunner::GuiRunner(int num_hardware_threads, int measurement_core, int period,
                     int duty_cycle, int cycles, PmTableReader &pm_table_reader,
                     size_t n_measurements,
                     const std::vector<int> &interesting_index)
    : num_hardware_threads_(num_hardware_threads),
      measurement_core_(measurement_core), period_ms_(period),
      duty_cycle_percent_(duty_cycle), num_cycles_(cycles),
      n_measurements_(n_measurements), interesting_index_(interesting_index),
      pm_table_reader_(pm_table_reader),
      spsc_queue_(1024), // SPSC queue size, e.g., for ~1 second of data
      gui_display_pointers_(interesting_index_.size()) {
  SPDLOG_INFO("GUI mode enabled. Initializing data buffers...");
  const size_t num_interesting = interesting_index_.size();

  for (size_t i = 0; i < num_interesting; ++i) {
    display_data_a_.push_back(std::make_unique<DisplayData>());
    display_data_b_.push_back(std::make_unique<DisplayData>());

    display_data_a_[i]->original_sensor_index = interesting_index_[i];
    display_data_b_[i]->original_sensor_index = interesting_index_[i];

    // Initially, point the GUI to buffer A
    gui_display_pointers_[i].store(display_data_a_[i].get());
  }
}

GuiRunner::~GuiRunner() {
  terminate_threads_.store(true);

  // Threads will be joined in run() before this destructor is called.
  if (window_) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
    glfwTerminate();
    SPDLOG_INFO("GUI resources cleaned up.");
  }
}

void GuiRunner::run_processing_thread() {
  enum class State { IDLE, CAPTURING } state = State::IDLE;
  TimePoint last_rise_time;
  std::vector<RawSample> current_trace;
  current_trace.reserve(period_ms_ + 50);

  const size_t num_interesting = interesting_index_.size();
  const int num_bins = period_ms_;

  // Accumulation Buffer: [sensor_idx][bin_idx] -> deque of samples
  std::vector<std::vector<std::deque<float>>> accumulation_buffer(
      num_interesting, std::vector<std::deque<float>>(num_bins));

  // Map original sensor index to its compact interesting_index
  std::unordered_map<int, size_t> sensor_to_storage_idx;
  for (size_t i = 0; i < interesting_index_.size(); ++i) {
    sensor_to_storage_idx[interesting_index_[i]] = i;
  }

  // Determine which buffer is the current write target
  const auto *write_buffer_ptr = &display_data_b_;

  while (!terminate_threads_.load()) {
    // 1. Process GUI commands
    if (GuiCommand cmd; command_queue_.try_pop(cmd)) {
      std::visit(
          [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, ChangeCoreCmd>) {
              SPDLOG_INFO("Processing command: Change core to {}",
                          arg.new_core_id);
              // Clear all data buffers to start fresh
              for (auto &sensor_bins : accumulation_buffer) {
                for (auto &bin : sensor_bins)
                  bin.clear();
              }
              current_trace.clear();
              state = State::IDLE;
            } else if constexpr (std::is_same_v<T, ChangeAccumulationsCmd>) {
              max_accumulations_.store(arg.new_count);
              SPDLOG_INFO("Processing command: Change accumulations to {}",
                          arg.new_count);
            }
          },
          cmd);
    }

    // 2. Consume raw samples from the SPSC queue
    RawSample sample;
    bool work_done = false;
    while (spsc_queue_.read(sample)) {
      work_done = true;

      if (bool last_state_is_idle = (state == State::IDLE);
          sample.worker_state == 1 && last_state_is_idle) {
        state = State::CAPTURING;
        last_rise_time = sample.timestamp;
        current_trace.clear();
      }

      if (state == State::CAPTURING) {
        auto time_since_rise =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                sample.timestamp - last_rise_time);

        if (long long time_delta_ms = time_since_rise.count();
            time_delta_ms >= 0 && time_delta_ms < period_ms_) {
          current_trace.push_back(sample);
        } else {
          state = State::IDLE;

          // --- KEY PROCESSING STEP ---
          // a) Regrid/Interpolate `current_trace` and add to
          // accumulation_buffer
          for (const auto &s : current_trace) {
            const long long bin_idx =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    s.timestamp - last_rise_time)
                    .count();
            if (bin_idx >= 0 && bin_idx < num_bins) {
              for (size_t i = 0; i < s.num_measurements; ++i) {
                if (const auto it = sensor_to_storage_idx.find(i);
                    it != sensor_to_storage_idx.end()) {
                  size_t storage_idx = it->second;
                  accumulation_buffer[storage_idx][bin_idx].push_back(
                      s.measurements[i]);
                }
              }
            }
          }

          // b) Trim accumulation buffer to max size
          int max_acc = max_accumulations_.load();
          for (auto &sensor_bins : accumulation_buffer) {
            for (auto &bin_deque : sensor_bins) {
              while (static_cast<int>(bin_deque.size()) > max_acc) {
                bin_deque.pop_front();
              }
            }
          }

          // c) Recalculate stats and populate the write buffer
          for (size_t i = 0; i < num_interesting; ++i) {
            auto &target_display = *(*write_buffer_ptr)[i];
            target_display.clear();
            target_display.window_after_ms = period_ms_;

            for (int bin_idx = 0; bin_idx < num_bins; ++bin_idx) {
              if (const auto &bin_deque = accumulation_buffer[i][bin_idx];
                  !bin_deque.empty()) {
                // Convert deque to vector for stats function
                // FIXME: Allocation occurs every 300ms, modifying
                // calculate_trimmed_mean to accept iterators or a std::span
                // would be better
                std::vector<float> bin_vec(bin_deque.begin(), bin_deque.end());

                target_display.x_data.push_back(static_cast<float>(bin_idx));
                target_display.y_data_mean.push_back(
                    calculate_trimmed_mean(bin_vec, 10.0f));
                target_display.y_data_min.push_back(
                    *std::ranges::min_element(bin_vec));
                target_display.y_data_max.push_back(
                    *std::ranges::max_element(bin_vec));
                target_display.accumulation_count = bin_vec.size();
              }
            }
          }

          // d) Atomically swap pointers
          for (size_t i = 0; i < num_interesting; ++i) {
            gui_display_pointers_[i].store((*write_buffer_ptr)[i].get(),
                                           std::memory_order_release);
          }

          // e) Flip the write buffer target for the next run
          write_buffer_ptr = (write_buffer_ptr == &display_data_a_)
                                 ? &display_data_b_
                                 : &display_data_a_;
        }
      }
    }

    if (!work_done) {
      std::this_thread::sleep_for(5ms);
    }
  }
}

// Member function to manage the worker thread's lifecycle
void GuiRunner::run_worker_thread() const {
  while (!terminate_threads_.load()) {
    if (manual_mode_.load()) {
      if (int core_to_test = manual_core_to_test_.load();
          core_to_test != measurement_core_) {
        worker_thread_func(core_to_test, period_ms_, duty_cycle_percent_,
                           num_cycles_);
      }
    }
    // In automatic mode, this thread would loop through cores.
    // For simplicity, we only implement manual mode continuous runs.
    std::this_thread::sleep_for(50ms);
  }
}

int GuiRunner::run() {
  if (!glfwInit()) { /* ... error handling ... */
    return -1;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  window_ = glfwCreateWindow(1600, 900, "PM Measure Tool", nullptr, nullptr);

  if (window_ == nullptr) {
    SPDLOG_ERROR("Failed to create GLFW window");
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window_);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGui_ImplGlfw_InitForOpenGL(window_, true);
  ImGui_ImplOpenGL3_Init("#version 330");
  ImGui::StyleColorsDark();

  // --- Launch all background threads ---
  g_run_measurement.store(true); // Measurement runs continuously
  std::thread measurement(measurement_thread_func, measurement_core_,
                          std::ref(spsc_queue_), std::ref(pm_table_reader_));
  std::thread processing(&GuiRunner::run_processing_thread, this);
  std::thread worker(&GuiRunner::run_worker_thread, this);

  while (!glfwWindowShouldClose(window_)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    std::string status = "Manual mode: testing core " +
                         std::to_string(manual_core_to_test_.load());

    render_gui(gui_display_pointers_, n_measurements_, interesting_index_,
               status, command_queue_, manual_mode_, manual_core_to_test_,
               num_hardware_threads_);

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window_, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window_);
  }

  // --- Cleanup ---
  terminate_threads_.store(true);
  g_run_measurement.store(false);

  // Join all threads
  if (measurement.joinable())
    measurement.join();
  if (processing.joinable())
    processing.join();
  if (worker.joinable())
    worker.join();

  SPDLOG_INFO("GUI mode finished.");
  return 0;
}