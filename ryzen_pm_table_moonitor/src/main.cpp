#include <chrono>
#include <iostream>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h>
#include <boost/pfr.hpp>
#include <string>
#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/pipeline.hpp>
#include <type_traits>
#include "pm_table_reader.hpp"
#include "stress_tester.hpp"
#include "analysis_manager.hpp"
#include "analysis.hpp"
#include "jitter_monitor.hpp"
#include <atomic> // For the stop flag

// Required headers for thread scheduling and affinity
#include <sched.h>
#include <pthread.h>
#include <cstring> // For strerror


// Helper function to create a scrolling buffer for plots
struct ScrollingBuffer {
    int              MaxSize;
    int              Offset;
    ImVector<ImVec2> Data;
    explicit ScrollingBuffer(int max_size = 2000) {
        MaxSize = max_size;
        Offset  = 0;
        Data.reserve(MaxSize);
    }
    void AddPoint(float x, float y) {
        if (Data.size() < MaxSize)
            Data.push_back(ImVec2(x, y));
        else {
            Data[Offset] = ImVec2(x, y);
            Offset       = (Offset + 1) % MaxSize;
        }
    }
    void Erase() {
        if (!Data.empty()) {
            Data.shrink(0);
            Offset = 0;
        }
    }
};

// Helper: Draw any struct in an ImGui table using Boost.PFR
template<typename T>
void DrawStructInTable(const char *table_id, const T &data) {
    if (!ImGui::BeginTable(table_id, 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        return;
    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    boost::pfr::for_each_field(data, [&](const auto &field, auto index) {
        auto member_name = boost::pfr::get_name<index, T>();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(std::string(member_name).c_str());
        ImGui::TableSetColumnIndex(1);
        using FieldType = std::decay_t<decltype(field)>;
        if constexpr (std::is_same_v<FieldType, float>) {
            ImGui::Text("%02.2f", field);
        } else if constexpr (std::is_same_v<FieldType, std::vector<float>>) {
            if (field.empty()) {
                ImGui::TextUnformatted("[ ]");
            } else {
                ImGui::BeginChild((std::string(member_name) + "_child").c_str(),
                                  ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 1.5f), false,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                for (size_t i = 0; i < field.size(); ++i) {
                    if (i > 0)
                        ImGui::SameLine();
                    ImGui::Text("%02.2f", field[i]);
                }
                ImGui::EndChild();
            }
        } else {
            ImGui::TextUnformatted("Unsupported Type");
        }
    });
    ImGui::EndTable();
}

// Helper function to generate distinct colors for the cores
ImVec4 generate_color_for_core(int core_id) {
    float hue = (float)core_id * 0.61803398875f; // Golden ratio
    hue = fmod(hue, 1.0f);
    ImVec4 color;
    ImGui::ColorConvertHSVtoRGB(hue, 0.85f, 0.95f, color.x, color.y, color.z);
    color.w = 1.0f;
    return color;
}

void RenderTextWithOutline(const char* text, const ImVec4& text_color)
{
    // Get the current draw list and cursor position
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 text_pos = ImGui::GetCursorScreenPos();

    // Calculate text size to advance the cursor and for hover detection
    ImVec2 text_size = ImGui::CalcTextSize(text);

    // Define colors
    ImU32 outline_col = IM_COL32(0, 0, 0, 255); // Black
    ImU32 text_col = ImGui::ColorConvertFloat4ToU32(text_color);

    // --- Render the outline ---
    // A 1-pixel outline is achieved by drawing the text in black at 4 diagonal locations.
    draw_list->AddText(ImVec2(text_pos.x - 1, text_pos.y - 1), outline_col, text);
    draw_list->AddText(ImVec2(text_pos.x + 1, text_pos.y - 1), outline_col, text);
    draw_list->AddText(ImVec2(text_pos.x - 1, text_pos.y + 1), outline_col, text);
    draw_list->AddText(ImVec2(text_pos.x + 1, text_pos.y + 1), outline_col, text);

    // --- Render the main text ---
    draw_list->AddText(text_pos, text_col, text);

    // --- Advance cursor and handle interaction ---
    // Use an InvisibleButton over the same area. This does two things:
    // 1. It advances the ImGui cursor by the size of the text, so the next item won't overlap.
    // 2. It makes the area interactive, so ImGui::IsItemHovered() works correctly.
    ImGui::InvisibleButton(text, text_size);
}



// ----------------------------------------------------------------------------
// Custom Worker Behavior to set high priority for a specific worker
// ----------------------------------------------------------------------------
class HighPriorityWorkerBehavior : public tf::WorkerInterface {
public:
    // This method is called by the executor before a worker thread enters the scheduling loop.
    void scheduler_prologue(tf::Worker& worker) override {

        // We will designate worker 0 as our high-priority, real-time worker.
        if (worker.id() == 0) {
            spdlog::info("Configuring high-priority scheduling for worker {}", worker.id());

            // --- ENABLE REAL-TIME SCHEDULING (from your old code) ---
            #if defined(__linux__)
            const int policy = SCHED_FIFO;
            sched_param params;
            // Set a high priority (1-99 for SCHED_FIFO).
            params.sched_priority = 80;

            int ret = pthread_setschedparam(worker.thread().native_handle(), policy, &params);
            if (ret != 0) {
                spdlog::error("Failed to set thread scheduling policy for worker {}. Error: {}", worker.id(), strerror(ret));
                spdlog::warn("You may need to run with sudo or grant CAP_SYS_NICE capabilities.");
            } else {
                spdlog::info("Successfully set worker {} scheduling policy to SCHED_FIFO with priority {}", worker.id(), params.sched_priority);
            }

            // --- SET CPU AFFINITY (from your old code) ---
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            // Pin the thread to a specific core, e.g., core 3
            const int core_id = 3;
            CPU_SET(core_id, &cpuset);

            ret = pthread_setaffinity_np(worker.thread().native_handle(), sizeof(cpu_set_t), &cpuset);
            if (ret != 0) {
                spdlog::error("Failed to set CPU affinity for worker {}. Error: {}", worker.id(), strerror(ret));
            } else {
                spdlog::info("Successfully pinned worker {} to CPU {}", worker.id(), core_id);
            }
            #else
            spdlog::warn("Real-time scheduling is only implemented for Linux in this example.");
            #endif

        } else {
            spdlog::info("Worker {} starting with default scheduling.", worker.id());
        }
    }

    // This method is called after a worker leaves the scheduling loop.
    void scheduler_epilogue(tf::Worker& worker, std::exception_ptr) override {
        spdlog::info("Worker {} left the work-stealing loop.", worker.id());
    }
};

int main() {
    spdlog::info("Starting PM Table Monitor");

    // Setup window
    if (!glfwInit()) {
        spdlog::error("Failed to initialize GLFW");
        return -1;
    }
    spdlog::info("GLFW initialized");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window = glfwCreateWindow(1280, 720, "PM Table Monitor", nullptr, nullptr);
    if (window == nullptr) {
        spdlog::error("Failed to create GLFW window");
        glfwTerminate();
        return -1;
    }
    spdlog::info("GLFW window created");
    glfwMakeContextCurrent(window);
    // gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 1. === Centralized Concurrency Setup ===
    const size_t num_workers = 4;
    tf::Executor executor(num_workers, tf::make_worker_interface<HighPriorityWorkerBehavior>());
    std::atomic<bool> stop_pipeline{false};

    // 2. === Instantiate Simplified Components ===
    StressTester stress_tester;
    AnalysisManager analysis_manager;
    PMTableReader pm_table_reader;

    // 3. === Define the Data Processing Pipeline ===

    // This is the number of concurrent data packets that can be "in-flight".
    const size_t num_concurrent_pipelines = 4;

    // The shared data buffer for the pipeline stages to communicate through.
    std::vector<TimestampedData> data_buffer(num_concurrent_pipelines);

    tf::Taskflow taskflow("PM_Table_Pipeline");
    tf::Pipeline pipeline(num_concurrent_pipelines,
        // Stage 1: Producer (Reads from file and WRITES to the shared buffer)
        tf::Pipe{tf::PipeType::SERIAL, [&](tf::Pipeflow& pf) {
            if (stop_pipeline.load(std::memory_order_relaxed)) {
                pf.stop();
                return;
            }

            static std::ifstream pm_table_file(pm_table_reader.pm_table_path_, std::ios::binary);
            static auto read_buffer = std::vector<float>(1024);
            static int bytes_to_read = -1;
            const std::chrono::microseconds target_period{1000};

            static JitterMonitor jitter_monitor(target_period.count(), 5000 /* samples before reporting */, 2500);
            static std::chrono::time_point<std::chrono::steady_clock> last_read_time = std::chrono::steady_clock::now();

            if (!pm_table_file.is_open()) {
                if(!stop_pipeline) spdlog::error("PMTableReader: Failed to open file: {}", pm_table_reader.pm_table_path_);
                stop_pipeline = true;
                pf.stop();
                return;
            }
            if (bytes_to_read == -1) {
                pm_table_file.read(reinterpret_cast<char*>(read_buffer.data()), read_buffer.size() * sizeof(float));
                int bytes_read = pm_table_file.gcount();
                if (bytes_read > 0) {
                     bytes_to_read = (bytes_read / sizeof(float)) * sizeof(float);
                     read_buffer.resize(bytes_read / sizeof(float));
                     spdlog::info("PMTableReader: Detected PM table size of {} bytes.", bytes_to_read);
                } else {
                     spdlog::error("PMTableReader: Failed to get initial PM table size.");
                     stop_pipeline = true;
                     pf.stop();
                     return;
                }
            }

            static auto next_wakeup = std::chrono::high_resolution_clock::now() + target_period;

            pm_table_file.clear();
            pm_table_file.seekg(0, std::ios::beg);
            std::this_thread::sleep_until(next_wakeup);

            auto timestamp = std::chrono::steady_clock::now();

            pm_table_file.read(reinterpret_cast<char*>(read_buffer.data()), bytes_to_read);

            if (pm_table_file.gcount() > 0) {
                long long timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp.time_since_epoch()).count();

                // Place the result in the shared buffer at the current pipeline's line index.
                data_buffer[pf.line()] = {timestamp_ns, read_buffer};
            }
            long long period_us = std::chrono::duration_cast<std::chrono::microseconds>(timestamp - last_read_time).count();
            jitter_monitor.record_sample(period_us);
            last_read_time = timestamp;
            next_wakeup += target_period;
        }},

        // Stage 2: Consumer (READS from the shared buffer and processes data)
        tf::Pipe{tf::PipeType::PARALLEL, [&](tf::Pipeflow& pf) {
            // Get a reference to the data produced by stage 1 on the same line.
            const TimestampedData& data = data_buffer[pf.line()];

            // 2a. Update the high-frequency analysis data
            analysis_manager.process_data_packet(data);

            // 2b. Update the decoded data for the GUI's "Decoded View" tab
            {
                std::lock_guard<std::mutex> lock(pm_table_reader.data_mutex_);
                pm_table_reader.latest_data_ = parse_pm_table_0x400005(data.data);
            }
        }}
    );


    // Add the pipeline to the main taskflow
    taskflow.composed_of(pipeline);

    // 4. === Run the Pipeline ===
    // This runs the pipeline indefinitely until pf.stop() is called.
    executor.run(taskflow);


    // Buffers for grouped plots
    // Frequencies
    std::vector<ScrollingBuffer> core_freq_buffers(8, ScrollingBuffer(2000));
    std::vector<ScrollingBuffer> core_freq_eff_buffers(8, ScrollingBuffer(2000));
    ScrollingBuffer              fclk_freq_buffer(2000), fclk_freq_eff_buffer(2000), uclk_freq_buffer(2000),
            memclk_freq_buffer(2000), gfx_freq_buffer(2000);

    // Powers
    std::vector<ScrollingBuffer> core_power_buffers(8, ScrollingBuffer(2000));
    ScrollingBuffer              vddcr_cpu_power_buffer(2000), vddcr_soc_power_buffer(2000), socket_power_buffer(2000),
            package_power_buffer(2000);

    // Temperatures
    std::vector<ScrollingBuffer> core_temp_buffers(8, ScrollingBuffer(2000));
    ScrollingBuffer              soc_temp_buffer(2000), peak_temp_buffer(2000), gfx_temp_buffer(2000);

    // Voltages
    std::vector<ScrollingBuffer> core_voltage_buffers(8, ScrollingBuffer(2000));
    ScrollingBuffer              peak_voltage_buffer(2000), max_soc_voltage_buffer(2000), gfx_voltage_buffer(2000),
            vid_limit_buffer(2000), vid_value_buffer(2000);

    // Limits and values (ppt, tdc, edc, etc.)
    ScrollingBuffer stapm_limit_buffer(2000), stapm_value_buffer(2000);
    ScrollingBuffer ppt_limit_buffer(2000), ppt_value_buffer(2000), ppt_limit_fast_buffer(2000),
            ppt_value_fast_buffer(2000), ppt_limit_apu_buffer(2000), ppt_value_apu_buffer(2000);
    ScrollingBuffer tdc_limit_buffer(2000), tdc_value_buffer(2000), tdc_limit_soc_buffer(2000),
            tdc_value_soc_buffer(2000);
    ScrollingBuffer edc_limit_buffer(2000), edc_value_buffer(2000);
    ScrollingBuffer thm_limit_buffer(2000), thm_value_buffer(2000);
    ScrollingBuffer fit_limit_buffer(2000), fit_value_buffer(2000);

    std::vector<ImVec4> core_colors;
    for(int i = 0; i < std::thread::hardware_concurrency(); ++i) {
        core_colors.push_back(generate_color_for_core(i));
    }

    spdlog::info("Entering main loop");
    while (!glfwWindowShouldClose(window)) {
        float history = 10.0f;
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        static float t = 0;

        // disable demo windows in release build
#ifndef NDEBUG
        ImGui::ShowDemoWindow();
        ImPlot::ShowDemoWindow();
#endif

        // --- All analysis logic is GONE from the main loop ---

        auto data = pm_table_reader.get_latest_data();
        if (data) {
            t += ImGui::GetIO().DeltaTime;

            // Frequencies
            for (size_t i = 0; i < data->core_freq.size() && i < core_freq_buffers.size(); ++i)
                core_freq_buffers[i].AddPoint(t, data->core_freq[i]);
            for (size_t i = 0; i < data->core_freq_eff.size() && i < core_freq_eff_buffers.size(); ++i)
                core_freq_eff_buffers[i].AddPoint(t, data->core_freq_eff[i]);
            fclk_freq_buffer.AddPoint(t, data->fclk_freq);
            fclk_freq_eff_buffer.AddPoint(t, data->fclk_freq_eff);
            uclk_freq_buffer.AddPoint(t, data->uclk_freq);
            memclk_freq_buffer.AddPoint(t, data->memclk_freq);
            gfx_freq_buffer.AddPoint(t, data->gfx_freq);

            // Powers
            for (size_t i = 0; i < data->core_power.size() && i < core_power_buffers.size(); ++i)
                core_power_buffers[i].AddPoint(t, data->core_power[i]);
            vddcr_cpu_power_buffer.AddPoint(t, data->vddcr_cpu_power);
            vddcr_soc_power_buffer.AddPoint(t, data->vddcr_soc_power);
            socket_power_buffer.AddPoint(t, data->socket_power);
            package_power_buffer.AddPoint(t, data->package_power);

            // Temperatures
            for (size_t i = 0; i < data->core_temp.size() && i < core_temp_buffers.size(); ++i)
                core_temp_buffers[i].AddPoint(t, data->core_temp[i]);
            soc_temp_buffer.AddPoint(t, data->soc_temp);
            peak_temp_buffer.AddPoint(t, data->peak_temp);
            gfx_temp_buffer.AddPoint(t, data->gfx_temp);

            // Voltages
            for (size_t i = 0; i < data->core_voltage.size() && i < core_voltage_buffers.size(); ++i)
                core_voltage_buffers[i].AddPoint(t, data->core_voltage[i]);
            peak_voltage_buffer.AddPoint(t, data->peak_voltage);
            max_soc_voltage_buffer.AddPoint(t, data->max_soc_voltage);
            gfx_voltage_buffer.AddPoint(t, data->gfx_voltage);
            vid_limit_buffer.AddPoint(t, data->vid_limit);
            vid_value_buffer.AddPoint(t, data->vid_value);

            // Limits and values
            stapm_limit_buffer.AddPoint(t, data->stapm_limit);
            stapm_value_buffer.AddPoint(t, data->stapm_value);
            ppt_limit_buffer.AddPoint(t, data->ppt_limit);
            ppt_value_buffer.AddPoint(t, data->ppt_value);
            ppt_limit_fast_buffer.AddPoint(t, data->ppt_limit_fast);
            ppt_value_fast_buffer.AddPoint(t, data->ppt_value_fast);
            ppt_limit_apu_buffer.AddPoint(t, data->ppt_limit_apu);
            ppt_value_apu_buffer.AddPoint(t, data->ppt_value_apu);
            tdc_limit_buffer.AddPoint(t, data->tdc_limit);
            tdc_value_buffer.AddPoint(t, data->tdc_value);
            tdc_limit_soc_buffer.AddPoint(t, data->tdc_limit_soc);
            tdc_value_soc_buffer.AddPoint(t, data->tdc_value_soc);
            edc_limit_buffer.AddPoint(t, data->edc_limit);
            edc_value_buffer.AddPoint(t, data->edc_value);
            thm_limit_buffer.AddPoint(t, data->thm_limit);
            thm_value_buffer.AddPoint(t, data->thm_value);
            fit_limit_buffer.AddPoint(t, data->fit_limit);
            fit_value_buffer.AddPoint(t, data->fit_value);
        }

        static ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize( viewport->Size);

#ifdef NDEBUG
        ImGui::Begin("PM Table Monitor", nullptr, flags);
#else
        ImGui::Begin("PM Table Monitor");
#endif
        if (ImGui::BeginTabBar("MainTabBar")) {

            // --- Tab 1: Decoded View ---
            if (ImGui::BeginTabItem("Decoded View")) {
                // Show current values in a table using Boost.PFR
                if (data) {
                    ImGui::TextUnformatted("Current PM Table Values:");
                    DrawStructInTable("PMTableDataTable", *data);
                }

                // Frequencies
                if (ImPlot::BeginPlot("Frequencies")) {
                    ImPlot::SetupAxes("Time", "Frequency (MHz)");
                    ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
                    for (size_t i = 0; i < core_freq_buffers.size(); ++i)
                        if (!core_freq_buffers[i].Data.empty())
                            ImPlot::PlotLine(("Core Freq " + std::to_string(i)).c_str(), &core_freq_buffers[i].Data[0].x,
                                             &core_freq_buffers[i].Data[0].y, core_freq_buffers[i].Data.size(), 0,
                                             core_freq_buffers[i].Offset, sizeof(ImVec2));
                    for (size_t i = 0; i < core_freq_eff_buffers.size(); ++i)
                        if (!core_freq_eff_buffers[i].Data.empty())
                            ImPlot::PlotLine(("Core EffFreq " + std::to_string(i)).c_str(), &core_freq_eff_buffers[i].Data[0].x,
                                             &core_freq_eff_buffers[i].Data[0].y, core_freq_eff_buffers[i].Data.size(), 0,
                                             core_freq_eff_buffers[i].Offset, sizeof(ImVec2));
                    if (!fclk_freq_buffer.Data.empty())
                        ImPlot::PlotLine("FCLK", &fclk_freq_buffer.Data[0].x, &fclk_freq_buffer.Data[0].y,
                                         fclk_freq_buffer.Data.size(), 0, fclk_freq_buffer.Offset, sizeof(ImVec2));
                    if (!fclk_freq_eff_buffer.Data.empty())
                        ImPlot::PlotLine("FCLK Eff", &fclk_freq_eff_buffer.Data[0].x, &fclk_freq_eff_buffer.Data[0].y,
                                         fclk_freq_eff_buffer.Data.size(), 0, fclk_freq_eff_buffer.Offset, sizeof(ImVec2));
                    if (!uclk_freq_buffer.Data.empty())
                        ImPlot::PlotLine("UCLK", &uclk_freq_buffer.Data[0].x, &uclk_freq_buffer.Data[0].y,
                                         uclk_freq_buffer.Data.size(), 0, uclk_freq_buffer.Offset, sizeof(ImVec2));
                    if (!memclk_freq_buffer.Data.empty())
                        ImPlot::PlotLine("MEMCLK", &memclk_freq_buffer.Data[0].x, &memclk_freq_buffer.Data[0].y,
                                         memclk_freq_buffer.Data.size(), 0, memclk_freq_buffer.Offset, sizeof(ImVec2));
                    if (!gfx_freq_buffer.Data.empty())
                        ImPlot::PlotLine("GFX Freq", &gfx_freq_buffer.Data[0].x, &gfx_freq_buffer.Data[0].y,
                                         gfx_freq_buffer.Data.size(), 0, gfx_freq_buffer.Offset, sizeof(ImVec2));
                    ImPlot::EndPlot();
                }

                // Powers
                if (ImPlot::BeginPlot("Powers")) {
                    ImPlot::SetupAxes("Time", "Power (W)");
                    ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
                    for (size_t i = 0; i < core_power_buffers.size(); ++i)
                        if (!core_power_buffers[i].Data.empty())
                            ImPlot::PlotLine(("Core Power " + std::to_string(i)).c_str(), &core_power_buffers[i].Data[0].x,
                                             &core_power_buffers[i].Data[0].y, core_power_buffers[i].Data.size(), 0,
                                             core_power_buffers[i].Offset, sizeof(ImVec2));
                    if (!vddcr_cpu_power_buffer.Data.empty())
                        ImPlot::PlotLine("VDDCR CPU", &vddcr_cpu_power_buffer.Data[0].x, &vddcr_cpu_power_buffer.Data[0].y,
                                         vddcr_cpu_power_buffer.Data.size(), 0, vddcr_cpu_power_buffer.Offset, sizeof(ImVec2));
                    if (!vddcr_soc_power_buffer.Data.empty())
                        ImPlot::PlotLine("VDDCR SOC", &vddcr_soc_power_buffer.Data[0].x, &vddcr_soc_power_buffer.Data[0].y,
                                         vddcr_soc_power_buffer.Data.size(), 0, vddcr_soc_power_buffer.Offset, sizeof(ImVec2));
                    if (!socket_power_buffer.Data.empty())
                        ImPlot::PlotLine("Socket", &socket_power_buffer.Data[0].x, &socket_power_buffer.Data[0].y,
                                         socket_power_buffer.Data.size(), 0, socket_power_buffer.Offset, sizeof(ImVec2));
                    if (!package_power_buffer.Data.empty())
                        ImPlot::PlotLine("Package", &package_power_buffer.Data[0].x, &package_power_buffer.Data[0].y,
                                         package_power_buffer.Data.size(), 0, package_power_buffer.Offset, sizeof(ImVec2));
                    ImPlot::EndPlot();
                }

                // Temperatures
                if (ImPlot::BeginPlot("Temperatures")) {
                    ImPlot::SetupAxes("Time", "Temperature (C)");
                    ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
                    for (size_t i = 0; i < core_temp_buffers.size(); ++i)
                        if (!core_temp_buffers[i].Data.empty())
                            ImPlot::PlotLine(("Core Temp " + std::to_string(i)).c_str(), &core_temp_buffers[i].Data[0].x,
                                             &core_temp_buffers[i].Data[0].y, core_temp_buffers[i].Data.size(), 0,
                                             core_temp_buffers[i].Offset, sizeof(ImVec2));
                    if (!soc_temp_buffer.Data.empty())
                        ImPlot::PlotLine("SoC", &soc_temp_buffer.Data[0].x, &soc_temp_buffer.Data[0].y,
                                         soc_temp_buffer.Data.size(), 0, soc_temp_buffer.Offset, sizeof(ImVec2));
                    if (!peak_temp_buffer.Data.empty())
                        ImPlot::PlotLine("Peak", &peak_temp_buffer.Data[0].x, &peak_temp_buffer.Data[0].y,
                                         peak_temp_buffer.Data.size(), 0, peak_temp_buffer.Offset, sizeof(ImVec2));
                    if (!gfx_temp_buffer.Data.empty())
                        ImPlot::PlotLine("GFX", &gfx_temp_buffer.Data[0].x, &gfx_temp_buffer.Data[0].y,
                                         gfx_temp_buffer.Data.size(), 0, gfx_temp_buffer.Offset, sizeof(ImVec2));
                    ImPlot::EndPlot();
                }

                // Voltages
                if (ImPlot::BeginPlot("Voltages")) {
                    ImPlot::SetupAxes("Time", "Voltage (V)");
                    ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
                    for (size_t i = 0; i < core_voltage_buffers.size(); ++i)
                        if (!core_voltage_buffers[i].Data.empty())
                            ImPlot::PlotLine(("Core Voltage " + std::to_string(i)).c_str(), &core_voltage_buffers[i].Data[0].x,
                                             &core_voltage_buffers[i].Data[0].y, core_voltage_buffers[i].Data.size(), 0,
                                             core_voltage_buffers[i].Offset, sizeof(ImVec2));
                    if (!peak_voltage_buffer.Data.empty())
                        ImPlot::PlotLine("Peak", &peak_voltage_buffer.Data[0].x, &peak_voltage_buffer.Data[0].y,
                                         peak_voltage_buffer.Data.size(), 0, peak_voltage_buffer.Offset, sizeof(ImVec2));
                    if (!max_soc_voltage_buffer.Data.empty())
                        ImPlot::PlotLine("Max SoC", &max_soc_voltage_buffer.Data[0].x, &max_soc_voltage_buffer.Data[0].y,
                                         max_soc_voltage_buffer.Data.size(), 0, max_soc_voltage_buffer.Offset, sizeof(ImVec2));
                    if (!gfx_voltage_buffer.Data.empty())
                        ImPlot::PlotLine("GFX", &gfx_voltage_buffer.Data[0].x, &gfx_voltage_buffer.Data[0].y,
                                         gfx_voltage_buffer.Data.size(), 0, gfx_voltage_buffer.Offset, sizeof(ImVec2));
                    if (!vid_limit_buffer.Data.empty())
                        ImPlot::PlotLine("VID Limit", &vid_limit_buffer.Data[0].x, &vid_limit_buffer.Data[0].y,
                                         vid_limit_buffer.Data.size(), 0, vid_limit_buffer.Offset, sizeof(ImVec2));
                    if (!vid_value_buffer.Data.empty())
                        ImPlot::PlotLine("VID Value", &vid_value_buffer.Data[0].x, &vid_value_buffer.Data[0].y,
                                         vid_value_buffer.Data.size(), 0, vid_value_buffer.Offset, sizeof(ImVec2));
                    ImPlot::EndPlot();
                }

                // Limits and values (PPT, TDC, EDC, etc.)
                if (ImPlot::BeginPlot("Limits & Values")) {
                    ImPlot::SetupAxes("Time", "Value");
                    ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
                    if (!stapm_limit_buffer.Data.empty())
                        ImPlot::PlotLine("STAPM Limit", &stapm_limit_buffer.Data[0].x, &stapm_limit_buffer.Data[0].y,
                                         stapm_limit_buffer.Data.size(), 0, stapm_limit_buffer.Offset, sizeof(ImVec2));
                    if (!stapm_value_buffer.Data.empty())
                        ImPlot::PlotLine("STAPM Value", &stapm_value_buffer.Data[0].x, &stapm_value_buffer.Data[0].y,
                                         stapm_value_buffer.Data.size(), 0, stapm_value_buffer.Offset, sizeof(ImVec2));
                    if (!ppt_limit_buffer.Data.empty())
                        ImPlot::PlotLine("PPT Limit", &ppt_limit_buffer.Data[0].x, &ppt_limit_buffer.Data[0].y,
                                         ppt_limit_buffer.Data.size(), 0, ppt_limit_buffer.Offset, sizeof(ImVec2));
                    if (!ppt_value_buffer.Data.empty())
                        ImPlot::PlotLine("PPT Value", &ppt_value_buffer.Data[0].x, &ppt_value_buffer.Data[0].y,
                                         ppt_value_buffer.Data.size(), 0, ppt_value_buffer.Offset, sizeof(ImVec2));
                    if (!ppt_limit_fast_buffer.Data.empty())
                        ImPlot::PlotLine("PPT Limit Fast", &ppt_limit_fast_buffer.Data[0].x, &ppt_limit_fast_buffer.Data[0].y,
                                         ppt_limit_fast_buffer.Data.size(), 0, ppt_limit_fast_buffer.Offset, sizeof(ImVec2));
                    if (!ppt_value_fast_buffer.Data.empty())
                        ImPlot::PlotLine("PPT Value Fast", &ppt_value_fast_buffer.Data[0].x, &ppt_value_fast_buffer.Data[0].y,
                                         ppt_value_fast_buffer.Data.size(), 0, ppt_value_fast_buffer.Offset, sizeof(ImVec2));
                    if (!ppt_limit_apu_buffer.Data.empty())
                        ImPlot::PlotLine("PPT Limit APU", &ppt_limit_apu_buffer.Data[0].x, &ppt_limit_apu_buffer.Data[0].y,
                                         ppt_limit_apu_buffer.Data.size(), 0, ppt_limit_apu_buffer.Offset, sizeof(ImVec2));
                    if (!ppt_value_apu_buffer.Data.empty())
                        ImPlot::PlotLine("PPT Value APU", &ppt_value_apu_buffer.Data[0].x, &ppt_value_apu_buffer.Data[0].y,
                                         ppt_value_apu_buffer.Data.size(), 0, ppt_value_apu_buffer.Offset, sizeof(ImVec2));
                    if (!tdc_limit_buffer.Data.empty())
                        ImPlot::PlotLine("TDC Limit", &tdc_limit_buffer.Data[0].x, &tdc_limit_buffer.Data[0].y,
                                         tdc_limit_buffer.Data.size(), 0, tdc_limit_buffer.Offset, sizeof(ImVec2));
                    if (!tdc_value_buffer.Data.empty())
                        ImPlot::PlotLine("TDC Value", &tdc_value_buffer.Data[0].x, &tdc_value_buffer.Data[0].y,
                                         tdc_value_buffer.Data.size(), 0, tdc_value_buffer.Offset, sizeof(ImVec2));
                    if (!tdc_limit_soc_buffer.Data.empty())
                        ImPlot::PlotLine("TDC Limit SoC", &tdc_limit_soc_buffer.Data[0].x, &tdc_limit_soc_buffer.Data[0].y,
                                         tdc_limit_soc_buffer.Data.size(), 0, tdc_limit_soc_buffer.Offset, sizeof(ImVec2));
                    if (!tdc_value_soc_buffer.Data.empty())
                        ImPlot::PlotLine("TDC Value SoC", &tdc_value_soc_buffer.Data[0].x, &tdc_value_soc_buffer.Data[0].y,
                                         tdc_value_soc_buffer.Data.size(), 0, tdc_value_soc_buffer.Offset, sizeof(ImVec2));
                    if (!edc_limit_buffer.Data.empty())
                        ImPlot::PlotLine("EDC Limit", &edc_limit_buffer.Data[0].x, &edc_limit_buffer.Data[0].y,
                                         edc_limit_buffer.Data.size(), 0, edc_limit_buffer.Offset, sizeof(ImVec2));
                    if (!edc_value_buffer.Data.empty())
                        ImPlot::PlotLine("EDC Value", &edc_value_buffer.Data[0].x, &edc_value_buffer.Data[0].y,
                                         edc_value_buffer.Data.size(), 0, edc_value_buffer.Offset, sizeof(ImVec2));
                    if (!thm_limit_buffer.Data.empty())
                        ImPlot::PlotLine("THM Limit", &thm_limit_buffer.Data[0].x, &thm_limit_buffer.Data[0].y,
                                         thm_limit_buffer.Data.size(), 0, thm_limit_buffer.Offset, sizeof(ImVec2));
                    if (!thm_value_buffer.Data.empty())
                        ImPlot::PlotLine("THM Value", &thm_value_buffer.Data[0].x, &thm_value_buffer.Data[0].y,
                                         thm_value_buffer.Data.size(), 0, thm_value_buffer.Offset, sizeof(ImVec2));
                    if (!fit_limit_buffer.Data.empty())
                        ImPlot::PlotLine("FIT Limit", &fit_limit_buffer.Data[0].x, &fit_limit_buffer.Data[0].y,
                                         fit_limit_buffer.Data.size(), 0, fit_limit_buffer.Offset, sizeof(ImVec2));
                    if (!fit_value_buffer.Data.empty())
                        ImPlot::PlotLine("FIT Value", &fit_value_buffer.Data[0].x, &fit_value_buffer.Data[0].y,
                                         fit_value_buffer.Data.size(), 0, fit_value_buffer.Offset, sizeof(ImVec2));
                    ImPlot::EndPlot();
                }

                ImGui::EndTabItem();
            }

            // --- Tab 3: Correlation Analysis (Now much simpler) ---
            if (ImGui::BeginTabItem("Correlation Analysis")) {
                if (stress_tester.is_running()) {
                    if (ImGui::Button("Stop Stress Threads")) { stress_tester.stop(); }
                } else {
                    if (ImGui::Button("Start Stress Threads")) { stress_tester.start(); }
                }
                ImGui::SameLine();
                if (ImGui::Button("Run Analysis")) {
                    if (stress_tester.is_running()) {
                        // 5. === Submit Analysis as a Detached Task ===
                        // This runs the heavy analysis on the executor without blocking the GUI or the pipeline.
                        executor.silent_async([&]() {
                            analysis_manager.run_correlation_analysis(&stress_tester);
                        });
                        spdlog::info("Analysis task submitted.");
                    } else {
                        spdlog::warn("Start stress threads before running analysis.");
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Stats")) {
                    // Also submit as a task to ensure thread safety.
                    executor.silent_async([&]() {
                       analysis_manager.reset_stats();
                   });
                }

                ImGui::Text("Core Color Legend:"); ImGui::SameLine();
                for (int i=0; i < stress_tester.get_core_count(); ++i) {
                    ImGui::ColorButton(("##corecolor" + std::to_string(i)).c_str(), core_colors[i]);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Core %d", i);
                    ImGui::SameLine();
                }
                ImGui::NewLine();

                // --- Get pre-computed results and render ---
                auto analysis_results = analysis_manager.get_analysis_results();

                const int num_columns = 16;
                if (ImGui::BeginTable("AnalysisGrid", num_columns, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
                    for (int col = 0; col < num_columns; ++col) ImGui::TableSetupColumn(("+" + std::to_string(col)).c_str());
                    ImGui::TableHeadersRow();

                    for (int i = 0; i < analysis_results.size(); ++i) {
                        ImGui::PushID(i);
                        if (i % num_columns == 0) ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(i % num_columns);

                        const CellStats& stats = analysis_results[i];
                        ImVec4 cell_color = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
                        if (stats.dominant_core_id != -1 && stats.correlation_strength > .1f) {
                            ImVec4 base_color = core_colors[stats.dominant_core_id];
                            float h, s, v;
                            ImGui::ColorConvertRGBtoHSV(base_color.x, base_color.y, base_color.z, h, s, v);
                            //s *= stats.correlation_strength;
                            cell_color = ImVec4(h, s, v, 1.0f);
                            ImGui::ColorConvertHSVtoRGB(h, s, v, cell_color.x, cell_color.y, cell_color.z);
                        }
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::ColorConvertFloat4ToU32(cell_color));
                        bool is_interesting = stats.get_stddev() > 0.00001f;
                        ImVec4 text_color = is_interesting ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f)  // Yellow for interesting
                                       : ImGui::GetStyle().Colors[ImGuiCol_Text]; // Default text color otherwise
                        if (is_interesting) {
                            std::string formatted_text = fmt::format("{:8.2f}", stats.current_val);
                            RenderTextWithOutline(formatted_text.c_str(), text_color);
                        } else {
                            ImGui::Text("%8.2f", stats.current_val);
                        }

                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("Index: %5d, Bytes: %5d .. %5d", i, i * 4, i * 4 + 3);
                            // Chess index (row/col) A..H, 1..64
                            ImGui::Text("Chess Index: %c%d", 'A' + (i % 8), (i / 8));
                            ImGui::Separator();
                            ImGui::Text("Live: %8.3f", stats.current_val);
                            ImGui::Text("Min:  %8.3f", stats.min_val);
                            ImGui::Text("Max:  %8.3f", stats.max_val);
                            ImGui::Text("Mean: %8.3f", stats.mean);
                            ImGui::Text("StdDev: %8.3f", stats.get_stddev());
                            ImGui::Separator();
                            ImGui::Text("Dominant Core: %d", stats.dominant_core_id);
                            ImGui::Text("Corr. Strength:   %.3f", stats.correlation_strength);

                            //  Display the quality score and color indicator
                            ImGui::Text("Corr. Quality:    %.3f", stats.correlation_quality);
                            ImVec4 quality_color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Red
                            if (stats.correlation_quality > 0.75f) quality_color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); // Green
                            else if (stats.correlation_quality > 0.4f) quality_color = ImVec4(1.0f, 1.0f, 0.3f, 1.0f); // Yellow
                            ImGui::TextColored(quality_color, "Quality Indicator");

                            // --- Realtime hover graph ---
                            ImGui::Separator();
                            ImGui::Text("History (%zu samples):", stats.history.size());
                            if (stats.history.size() > 1) {
                                std::vector<float> timestamps;
                                std::vector<float> values;
                                timestamps.reserve(stats.history.size());
                                values.reserve(stats.history.size());
                                long long first_ts = stats.history.front().timestamp_ns;
                                for (const auto& sample : stats.history) {
                                    timestamps.push_back((float)(sample.timestamp_ns - first_ts) / 1e9f);
                                    values.push_back(sample.value);
                                }

                                if (ImPlot::BeginPlot("##History", ImVec2(400, 200), ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
                                    ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
                                    float y_min = stats.min_val, y_max = stats.max_val;
                                    float padding = (y_max - y_min) * 0.1f;
                                    padding = (padding < 1e-5f) ? 1.0f : padding;
                                    ImPlot::SetupAxisLimits(ImAxis_Y1, y_min - padding, y_max + padding, ImGuiCond_Always);
                                    ImPlot::SetupAxisLimits(ImAxis_X1, timestamps.front(), timestamps.back(), ImGuiCond_Always);
                                    ImPlot::PlotLine("Value", timestamps.data(), values.data(), (int)timestamps.size());
                                    ImPlot::EndPlot();
                                }
                            }
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        // --- END: Tab Bar ---

        ImGui::End();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    spdlog::info("Exiting main loop...");
    // 6. === Coordinated Shutdown ===
    stop_pipeline = true;  // Signal the pipeline to stop producing tokens
    executor.wait_for_all(); // Wait for all tasks, including the pipeline, to finish
    stress_tester.stop();

    // --- Cleanup ---
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    spdlog::info("Shutdown complete");
    return 0;
}
