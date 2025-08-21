//
// Created by kiel on 8/21/25.
//#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "pm_table_reader.hpp"
#include <taskflow/taskflow.hpp>

// Helper function to create a scrolling buffer for plots
struct ScrollingBuffer {
    int MaxSize;
    int Offset;
    ImVector<ImVec2> Data;
    ScrollingBuffer(int max_size = 2000) {
        MaxSize = max_size;
        Offset = 0;
        Data.reserve(MaxSize);
    }
    void AddPoint(float x, float y) {
        if (Data.size() < MaxSize)
            Data.push_back(ImVec2(x, y));
        else {
            Data[Offset] = ImVec2(x, y);
            Offset = (Offset + 1) % MaxSize;
        }
    }
    void Erase() {
        if (Data.size() > 0) {
            Data.shrink(0);
            Offset = 0;
        }
    }
};

int main() {
    // Setup window
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "PM Table Monitor", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    PMTableReader pm_table_reader;

    // TaskFlow setup
    tf::Executor executor;
    tf::Taskflow taskflow;

    taskflow.emplace([&](){
        pm_table_reader.start_reading();
    });

    executor.run(taskflow);

    std::vector<ScrollingBuffer> core_clock_buffers(8, ScrollingBuffer(2000));
    std::vector<ScrollingBuffer> core_power_buffers(8, ScrollingBuffer(2000));
    ScrollingBuffer package_power_buffer(2000);
    float history = 10.0f;

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        auto data = pm_table_reader.get_latest_data();
        if (data) {
            static float t = 0;
            t += ImGui::GetIO().DeltaTime;

            for (size_t i = 0; i < data->core_clocks.size() && i < core_clock_buffers.size(); ++i) {
                core_clock_buffers[i].AddPoint(t, data->core_clocks[i]);
            }
            for (size_t i = 0; i < data->core_powers.size() && i < core_power_buffers.size(); ++i) {
                core_power_buffers[i].AddPoint(t, data->core_powers[i]);
            }
            package_power_buffer.AddPoint(t, data->package_power);
        }


        ImGui::Begin("PM Table Monitor");

        if (ImPlot::BeginPlot("Core Clocks")) {
            ImPlot::SetupAxes("Time", "Frequency (MHz)");
            ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
            for (size_t i = 0; i < core_clock_buffers.size(); ++i) {
                char label[32];
                snprintf(label, 32, "Core %zu", i);
                if (!core_clock_buffers[i].Data.empty()) {
                    ImPlot::PlotLine(label, &core_clock_buffers[i].Data[0].x, &core_clock_buffers[i].Data[0].y, core_clock_buffers[i].Data.size(), 0, core_clock_buffers[i].Offset, sizeof(ImVec2));
                }
            }
            ImPlot::EndPlot();
        }

        if (ImPlot::BeginPlot("Core Powers")) {
            ImPlot::SetupAxes("Time", "Power (W)");
            ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
            for (size_t i = 0; i < core_power_buffers.size(); ++i) {
                char label[32];
                snprintf(label, 32, "Core %zu", i);
                 if (!core_power_buffers[i].Data.empty()) {
                    ImPlot::PlotLine(label, &core_power_buffers[i].Data[0].x, &core_power_buffers[i].Data[0].y, core_power_buffers[i].Data.size(), 0, core_power_buffers[i].Offset, sizeof(ImVec2));
                }
            }
            ImPlot::EndPlot();
        }

        if (ImPlot::BeginPlot("Package Power")) {
            ImPlot::SetupAxes("Time", "Power (W)");
            ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
            if (!package_power_buffer.Data.empty()) {
                ImPlot::PlotLine("Package", &package_power_buffer.Data[0].x, &package_power_buffer.Data[0].y, package_power_buffer.Data.size(), 0, package_power_buffer.Offset, sizeof(ImVec2));
            }
            ImPlot::EndPlot();
        }

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

    pm_table_reader.stop_reading();

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}