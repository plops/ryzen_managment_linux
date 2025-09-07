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
#include <span>
#include <thread>

#include "pm_table_reader.hpp"
#include "stats_utils.hpp"

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
      measurement_core_(measurement_core),
      // The worker load period is now distinct from the capture window
      worker_period_ms_(period), duty_cycle_percent_(duty_cycle),
      num_cycles_(cycles), n_measurements_(n_measurements),
      interesting_index_(interesting_index), pm_table_reader_(pm_table_reader),
      spsc_queue_(600), // SPSC queue size, e.g., for ~2 seconds of data
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
  current_trace.reserve(window_after_ms_ + 50);

  // Buffer to hold recent samples for the pre-trigger window
  std::deque<RawSample> sample_history;
  const size_t history_size = window_before_ms_ + 10; // Keep ~50ms + margin

  const size_t num_interesting = interesting_index_.size();
  const int num_bins = window_before_ms_ + window_after_ms_;

  std::vector<std::vector<std::deque<float>>> accumulation_buffer(
      num_interesting, std::vector<std::deque<float>>(num_bins));

  std::unordered_map<int, size_t> sensor_to_storage_idx;
  for (size_t i = 0; i < interesting_index_.size(); ++i) {
    sensor_to_storage_idx[interesting_index_[i]] = i;
  }

  auto *write_buffer_ptr = &display_data_b_;
  int last_worker_state = 0;

  while (!terminate_threads_.load()) {
    if (GuiCommand cmd; command_queue_.try_pop(cmd)) {
      std::visit(
          [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, ChangeCoreCmd>) {
              SPDLOG_INFO("Processing command: Change core to {}",
                          arg.new_core_id);
              for (auto &sensor_bins : accumulation_buffer) {
                for (auto &bin : sensor_bins)
                  bin.clear();
              }
              current_trace.clear();
              sample_history.clear();
              state = State::IDLE;
            } else if constexpr (std::is_same_v<T, ChangeAccumulationsCmd>) {
              max_accumulations_.store(arg.new_count);
              SPDLOG_INFO("Processing command: Change accumulations to {}",
                          arg.new_count);
            }
          },
          cmd);
    }

    RawSample sample;
    bool work_done = false;
    while (spsc_queue_.read(sample)) {
      work_done = true;

      sample_history.push_back(sample);
      if (sample_history.size() > history_size) {
        sample_history.pop_front();
      }

      if (sample.worker_state == 1 && last_worker_state == 0) {
        state = State::CAPTURING;
        last_rise_time = sample.timestamp;
        current_trace.clear();
      }
      last_worker_state = sample.worker_state;

      if (state == State::CAPTURING) {
        auto time_since_rise =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                sample.timestamp - last_rise_time);

        if (long long time_delta_ms = time_since_rise.count();
            time_delta_ms >= 0 && time_delta_ms < window_after_ms_) {
          current_trace.push_back(sample);
        } else if (time_delta_ms >= window_after_ms_) {
          state = State::IDLE;

          auto process_sample_collection = [&](const auto &collection) {
            for (const auto &s : collection) {
              const long long time_delta =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      s.timestamp - last_rise_time)
                      .count();
              const long long bin_idx = time_delta + window_before_ms_;

              if (bin_idx >= 0 && bin_idx < num_bins) {
                for (size_t sens_idx = 0; sens_idx < s.num_measurements;
                     ++sens_idx) {
                  if (auto it = sensor_to_storage_idx.find(sens_idx);
                      it != sensor_to_storage_idx.end()) {
                    accumulation_buffer[it->second][bin_idx].push_back(
                        s.measurements[sens_idx]);
                  }
                }
              }
            }
          };

          process_sample_collection(sample_history);
          process_sample_collection(current_trace);

          int max_acc = max_accumulations_.load();
          for (auto &sensor_bins : accumulation_buffer) {
            for (auto &bin_deque : sensor_bins) {
              while (static_cast<int>(bin_deque.size()) > max_acc) {
                bin_deque.pop_front();
              }
            }
          }

          for (size_t i = 0; i < num_interesting; ++i) {
            auto &target_display = *(*write_buffer_ptr)[i];
            target_display.clear();
            target_display.window_before_ms = window_before_ms_;
            target_display.window_after_ms = window_after_ms_;
            target_display.accumulation_count =
                !accumulation_buffer[i].empty()
                    ? accumulation_buffer[i][window_before_ms_].size()
                    : 0;

            for (int bin_idx = 0; bin_idx < num_bins; ++bin_idx) {
              if (const auto &bin_deque = accumulation_buffer[i][bin_idx];
                  !bin_deque.empty()) {
                target_display.x_data.push_back(
                    static_cast<float>(bin_idx - window_before_ms_));

                std::vector<float> temp_vec(bin_deque.begin(), bin_deque.end());
                target_display.y_data_mean.push_back(
                    calculate_trimmed_mean(temp_vec, 10.0f));
                target_display.y_data_min.push_back(
                    *std::ranges::min_element(bin_deque));
                target_display.y_data_max.push_back(
                    *std::ranges::max_element(bin_deque));
              }
            }
          }

          for (size_t i = 0; i < num_interesting; ++i) {
            gui_display_pointers_[i].store((*write_buffer_ptr)[i].get(),
                                           std::memory_order_release);
          }

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

void GuiRunner::run_worker_thread() const {
  while (!terminate_threads_.load()) {
    if (manual_mode_.load()) {
      if (int core_to_test = manual_core_to_test_.load();
          core_to_test != measurement_core_) {
        worker_thread_func(core_to_test, worker_period_ms_, duty_cycle_percent_,
                           num_cycles_);
      }
    }
    std::this_thread::sleep_for(50ms);
  }
}

int GuiRunner::run() {
  if (!glfwInit()) {
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

  g_run_measurement.store(true);
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

  terminate_threads_.store(true);
  g_run_measurement.store(false);

  if (measurement.joinable())
    measurement.join();
  if (processing.joinable())
    processing.join();
  if (worker.joinable())
    worker.join();

  SPDLOG_INFO("GUI mode finished.");
  return 0;
}