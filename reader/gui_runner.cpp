#include "gui_runner.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "eye_capturer.hpp"
#include "eye_diagram.hpp"
#include "gui_components.hpp"
#include "measurement_types.hpp"
#include "pm_table_reader.hpp"
#include "popl.hpp"

// Forward declarations for thread functions from measure.cpp
void measurement_thread_func(int core_id,
                             std::vector<LocalMeasurement> &storage,
                             size_t &sample_count,
                             PmTableReader &pm_table_reader,
                             EyeCapturer &capturer);

void worker_thread_func(int core_id, int period_ms, int duty_cycle_percent,
                        int num_cycles, size_t &transition_count);

// Extern global flags from measure.cpp
extern std::atomic<bool> g_run_measurement;
extern std::atomic<bool> g_run_worker;

GuiRunner::GuiRunner(int rounds, int num_hardware_threads, int measurement_core,
                     int period, int duty_cycle, int cycles,
                     std::vector<LocalMeasurement> &measurement_view,
                     PmTableReader &pm_table_reader, size_t n_measurements,
                     const std::vector<int> &interesting_index)
    : rounds_(rounds), num_hardware_threads_(num_hardware_threads),
      measurement_core_(measurement_core), period_(period),
      duty_cycle_(duty_cycle), cycles_(cycles),
      measurement_view_(measurement_view), pm_table_reader_(pm_table_reader),
      n_measurements_(n_measurements), interesting_index_(interesting_index) {
  SPDLOG_INFO("GUI mode enabled. Initializing window and double-buffer...");
  // --- Initialize double-buffer for eye diagrams ---
  size_t expected_events = static_cast<int>(cycles * 1.3f);
  auto window_before_ms = 0;
  auto window_after_ms = period;
  storage_buffer_a_ = std::make_unique<EyeDiagramStorage>(
      interesting_index, expected_events, window_before_ms, window_after_ms);
  storage_buffer_b_ = std::make_unique<EyeDiagramStorage>(
      interesting_index, expected_events, window_before_ms, window_after_ms);
  gui_read_buffer_.store(storage_buffer_a_.get());
}

GuiRunner::~GuiRunner() {
  terminate_thread_.store(true); // Signal experiment thread to stop

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

void GuiRunner::run_experiment_thread() {
  std::atomic<bool> experiment_done = false;
  std::atomic<int> current_round = 0;
  std::atomic<int> current_core_testing = 0;

  // The experiment thread will write to the buffer not currently being read by
  // the GUI.
  EyeDiagramStorage *write_buffer = storage_buffer_b_.get();
  EyeCapturer capturer(*write_buffer);

  while (!terminate_thread_.load()) {
    bool is_manual = manual_mode_.load();

    if (is_manual) {
      // --- MANUAL MODE ---
      int core_to_test = manual_core_to_test_.load();
      if (core_to_test == measurement_core_) {
        // If user somehow selects the measurement core, pick a valid one.
        core_to_test = (measurement_core_ + 1) % num_hardware_threads_;
        if (core_to_test == 0)
          core_to_test = 1; // Ensure it's not 0
        manual_core_to_test_.store(core_to_test);
      }
      current_core_testing = core_to_test;
      experiment_done = false; // Manual mode is never "done"

      g_run_measurement = false;
      g_run_worker = false;
      size_t actual_sample_count = 0;
      size_t actual_transition_count = 0;
      write_buffer->clear();

      std::thread measurement_thread(
          measurement_thread_func, measurement_core_,
          std::ref(measurement_view_), std::ref(actual_sample_count),
          std::ref(pm_table_reader_), std::ref(capturer));

      std::thread worker_thread(worker_thread_func, core_to_test, period_,
                                duty_cycle_, cycles_,
                                std::ref(actual_transition_count));

      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      g_run_measurement.store(true, std::memory_order_release);
      g_run_worker.store(true, std::memory_order_release);

      worker_thread.join();
      g_run_measurement.store(false, std::memory_order_release);
      measurement_thread.join();

      gui_read_buffer_.store(write_buffer, std::memory_order_release);
      // write_buffer = (write_buffer == storage_buffer_a_.get())
      //                    ? storage_buffer_b_.get()
      //                    : storage_buffer_a_.get();
      // SPDLOG_INFO("Manual mode switch capturing into buffer {} at {:p}",
      //             (write_buffer == storage_buffer_a_.get()) ? 'A' : 'B',
      //             static_cast<void*>(write_buffer));
      capturer.set_storage(*write_buffer);

      // In manual mode, briefly pause to prevent pegging the CPU if cycles are
      // very short
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } else {
      // --- AUTOMATIC MODE ---
      experiment_done = false;
      for (int round = 0;
           round < rounds_ && !manual_mode_.load() && !terminate_thread_.load();
           ++round) {
        current_round = round + 1;
        for (int core_to_test = 1;
             core_to_test < num_hardware_threads_ && !manual_mode_.load() &&
             !terminate_thread_.load();
             ++core_to_test) {
          if (core_to_test == measurement_core_)
            continue;
          current_core_testing = core_to_test;

          g_run_measurement = false;
          g_run_worker = false;
          size_t actual_sample_count = 0;
          size_t actual_transition_count = 0;
          write_buffer->clear();

          std::thread measurement_thread(
              measurement_thread_func, measurement_core_,
              std::ref(measurement_view_), std::ref(actual_sample_count),
              std::ref(pm_table_reader_), std::ref(capturer));

          std::thread worker_thread(worker_thread_func, core_to_test, period_,
                                    duty_cycle_, cycles_,
                                    std::ref(actual_transition_count));

          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          g_run_measurement.store(true, std::memory_order_release);
          g_run_worker.store(true, std::memory_order_release);

          worker_thread.join();
          g_run_measurement.store(false, std::memory_order_release);
          measurement_thread.join();

          gui_read_buffer_.store(write_buffer, std::memory_order_release);
          write_buffer = (write_buffer == storage_buffer_a_.get())
                             ? storage_buffer_b_.get()
                             : storage_buffer_a_.get();
          SPDLOG_INFO("Automatic mode switch capturing into buffer {}",
                      (write_buffer == storage_buffer_a_.get()) ? 'A' : 'B');
          capturer.set_storage(*write_buffer);
        }
      }
      experiment_done = true;

      // Wait until mode is switched or thread is terminated
      while (!manual_mode_.load() && !terminate_thread_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }
  SPDLOG_INFO("Experiment thread terminated.");
}

int GuiRunner::run() {
  if (!glfwInit()) {
    SPDLOG_ERROR("Failed to initialize GLFW");
    return -1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  window_ = glfwCreateWindow(1280, 720, "Measure Tool - Live Eye Diagrams",
                             nullptr, nullptr);
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

  GuiDataCache gui_cache;
  std::atomic<bool> experiment_done = false;

  // The experiment thread now manages its own state variables.
  // We pass our new atomics into the render function.
  std::thread experiment_thread(&GuiRunner::run_experiment_thread, this);

  while (!glfwWindowShouldClose(window_)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    static auto last_update = std::chrono::steady_clock::now();
    if (std::chrono::steady_clock::now() - last_update >
        std::chrono::milliseconds(16)) {
      // --- Get the current read buffer atomically ---
      static EyeDiagramStorage *old_read_buffer = nullptr;
      EyeDiagramStorage *current_read_buffer =
          gui_read_buffer_.load(std::memory_order_acquire);
      if (old_read_buffer && old_read_buffer != current_read_buffer) {
        SPDLOG_INFO("Read buffer has changed: {:p}.",
                    reinterpret_cast<void *>(current_read_buffer));
      }
      old_read_buffer = current_read_buffer;
      gui_cache.update(*current_read_buffer);
      last_update = std::chrono::steady_clock::now();
    }

    std::string status;
    if (manual_mode_.load()) {
      status = "Manual mode: testing core " +
               std::to_string(manual_core_to_test_.load());
    } else {
      if (experiment_done) {
        // This needs to be communicated from the thread
        status = "Automatic scan complete. Switch to manual mode to continue.";
      } else {
        // This part is tricky as the thread owns these now.
        // For simplicity, we'll just show a generic message.
        status = "Running automatic scan...";
      }
    }

    render_gui(gui_cache, n_measurements_, interesting_index_, status,
               manual_mode_, manual_core_to_test_, num_hardware_threads_);

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window_, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window_);
  }

  terminate_thread_.store(true);
  if (experiment_thread.joinable()) {
    experiment_thread.join();
  }

  SPDLOG_INFO("GUI mode finished.");
  return 0;
}
