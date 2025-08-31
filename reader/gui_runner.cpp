#include "gui_runner.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <thread>
#include <atomic>
#include <chrono>

#include "popl.hpp"
#include "gui_components.hpp"
#include "measurement_types.hpp"
#include "pm_table_reader.hpp"
#include "eye_capturer.hpp"
#include "eye_diagram.hpp"

// Forward declarations for thread functions from measure.cpp
void measurement_thread_func(int core_id,
                             std::vector<LocalMeasurement> &storage,
                             size_t &sample_count,
                             PmTableReader &pm_table_reader,
                             EyeCapturer &capturer);

void worker_thread_func(int core_id,
                        int period_ms,
                        int duty_cycle_percent,
                        int num_cycles,
                        size_t &transition_count);

// Extern global flags from measure.cpp
extern std::atomic<bool> g_run_measurement;
extern std::atomic<bool> g_run_worker;

GuiRunner::GuiRunner(
    int rounds,
    int num_hardware_threads,
    int measurement_core,
    int period,
    int duty_cycle,
    int cycles,
    std::vector<LocalMeasurement>& measurement_view,
    PmTableReader& pm_table_reader,
    EyeCapturer& capturer, // Note: capturer is no longer stored as a member
    EyeDiagramStorage& eye_storage, // Note: eye_storage is no longer stored as a member
    size_t n_measurements,
    const std::vector<int>& interesting_index
) : rounds_(rounds),
    num_hardware_threads_(num_hardware_threads),
    measurement_core_(measurement_core),
    period_(period),
    duty_cycle_(duty_cycle),
    cycles_(cycles),
    measurement_view_(measurement_view),
    pm_table_reader_(pm_table_reader),
    n_measurements_(n_measurements),
    interesting_index_(interesting_index)
{
    SPDLOG_INFO("GUI mode enabled. Initializing window and double-buffer...");
    // --- NEW: Initialize double-buffer for eye diagrams ---
    size_t expected_events = static_cast<int>(cycles * 1.3f);
    storage_buffer_a_ = std::make_unique<EyeDiagramStorage>(interesting_index, expected_events);
    storage_buffer_b_ = std::make_unique<EyeDiagramStorage>(interesting_index, expected_events);
    gui_read_buffer_.store(storage_buffer_a_.get());
}

GuiRunner::~GuiRunner() {
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

void GuiRunner::run_experiment_thread(std::atomic<bool>& experiment_done, std::atomic<int>& current_round, std::atomic<int>& current_core_testing) {
    size_t actual_sample_count = 0;
    size_t actual_transition_count = 0;

    // The experiment thread will write to the buffer not currently being read by the GUI.
    EyeDiagramStorage* write_buffer = storage_buffer_b_.get();
    EyeCapturer capturer(*write_buffer);

    for (int round = 0; round < rounds_; ++round) {
        current_round = round + 1;
        for (int core_to_test = 1; core_to_test < num_hardware_threads_; ++core_to_test) {
            if (core_to_test == measurement_core_) continue;
            current_core_testing = core_to_test;

            g_run_measurement = false;
            g_run_worker = false;
            actual_sample_count = 0;
            actual_transition_count = 0;
            write_buffer->clear();

            std::thread measurement_thread(measurement_thread_func, measurement_core_,
                                           std::ref(measurement_view_), std::ref(actual_sample_count),
                                           std::ref(pm_table_reader_), std::ref(capturer));

            std::thread worker_thread(worker_thread_func, core_to_test,
                                      period_, duty_cycle_, cycles_,
                                      std::ref(actual_transition_count));

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            g_run_measurement.store(true, std::memory_order_release);
            g_run_worker.store(true, std::memory_order_release);

            worker_thread.join();
            g_run_measurement.store(false, std::memory_order_release);
            measurement_thread.join();

            // --- NEW: Atomically swap buffers for the GUI to read ---
            gui_read_buffer_.store(write_buffer, std::memory_order_release);

            // And get the other buffer for the next write cycle.
            write_buffer = (write_buffer == storage_buffer_a_.get()) ? storage_buffer_b_.get() : storage_buffer_a_.get();
            capturer.set_storage(*write_buffer); // Re-point the capturer to the new write buffer
        }
    }
    experiment_done = true;
}

int GuiRunner::run() {
    if (!glfwInit()) {
        SPDLOG_ERROR("Failed to initialize GLFW");
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window_ = glfwCreateWindow(1280, 720, "Measure Tool - Live Eye Diagrams", nullptr, nullptr);
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
    std::atomic<int> current_core_testing = 0;
    std::atomic<int> current_round = 0;

    std::thread experiment_thread(&GuiRunner::run_experiment_thread, this, std::ref(experiment_done), std::ref(current_round), std::ref(current_core_testing));

    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        static auto last_update = std::chrono::steady_clock::now();
        if (std::chrono::steady_clock::now() - last_update > std::chrono::milliseconds(16)) {
             // --- NEW: Get the current read buffer atomically ---
             EyeDiagramStorage* current_read_buffer = gui_read_buffer_.load(std::memory_order_acquire);
             gui_cache.update(*current_read_buffer);
             last_update = std::chrono::steady_clock::now();
        }

        std::string status = "Running round " + std::to_string(current_round.load()) + " on core " + std::to_string(current_core_testing.load());
        if (experiment_done) status = "Experiment Complete";

        render_gui(gui_cache, n_measurements_, interesting_index_, status);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }

    if (experiment_thread.joinable()) {
        experiment_thread.join();
    }

    SPDLOG_INFO("GUI mode finished.");
    return 0;
}
