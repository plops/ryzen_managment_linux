// This file contains the UI rendering logic, now decoupled from any data
// processing.

#include "imgui.h"
#include "implot.h"
#include "shared_data_types.hpp"
#include <algorithm> // For std::find
#include <atomic>
#include <string>
#include <vector>

void render_gui(
    const std::vector<std::atomic<DisplayData *>> &gui_display_pointers,
    int n_total_sensors, const std::vector<int> &interesting_indices,
    const std::string &experiment_status, CommandQueue &command_queue,
    std::atomic<bool> &manual_mode, std::atomic<int> &manual_core_to_test,
    int num_hardware_threads) {

#ifdef NDEBUG
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);
  ImGui::Begin("PM Table Eye Diagrams", nullptr, flags);
#else
  ImGui::Begin("PM Table Eye Diagrams");
#endif

  ImGui::Text("Experiment Status: %s", experiment_status.c_str());

  size_t accumulation_count = 0;
  for (const auto &atomic_ptr : gui_display_pointers) {
    const DisplayData *plot = atomic_ptr.load(std::memory_order_acquire);
    if (plot && plot->accumulation_count > 0) {
      accumulation_count = plot->accumulation_count;
      break;
    }
  }
  ImGui::Text("Accumulated Traces: %zu", accumulation_count);
  ImGui::Separator();

  bool is_manual = manual_mode.load();
  if (ImGui::Checkbox("Manual Control", &is_manual)) {
    manual_mode.store(is_manual);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!is_manual);

  int core_to_test = manual_core_to_test.load();
  if (ImGui::SliderInt("Test Core", &core_to_test, 1,
                       num_hardware_threads - 1)) {
    manual_core_to_test.store(core_to_test);
    command_queue.push(ChangeCoreCmd{core_to_test});
  }
  ImGui::EndDisabled();
  ImGui::Separator();

  if (ImGui::BeginTable("EyeDiagramGrid", 16)) {
    for (int i = 0; i < n_total_sensors; ++i) {
      ImGui::TableNextColumn();
      auto it = std::ranges::find(interesting_indices, i);
      const bool is_interesting = it != interesting_indices.end();
      ImGui::PushID(i);

      if (is_interesting) {
        const size_t interesting_idx =
            std::distance(interesting_indices.begin(), it);
        const DisplayData *plot = gui_display_pointers[interesting_idx].load(
            std::memory_order_acquire);

        if (plot && !plot->x_data.empty()) {
          if (ImPlot::BeginPlot("##EyePlot", ImVec2(-1, 80),
                                ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) {
            // --- FIXED: Use before and after windows for correct axis limits ---
            ImPlot::SetupAxisLimits(ImAxis_X1, -plot->window_before_ms,
                                    plot->window_after_ms, ImGuiCond_Always);
            ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_AutoFit);

            ImPlot::PushStyleColor(ImPlotCol_Line,
                                   ImVec4(1, 1, 0, 0.8f)); // Yellow for Mean
            ImPlot::PlotLine("TrimmedMean", plot->x_data.data(),
                             plot->y_data_mean.data(),
                             static_cast<int>(plot->x_data.size()));

            ImPlot::PushStyleColor(ImPlotCol_Line,
                                   ImVec4(1, 0, 0, 0.5f)); // Red for Max
            ImPlot::PlotLine("Max", plot->x_data.data(),
                             plot->y_data_max.data(),
                             static_cast<int>(plot->x_data.size()));

            ImPlot::PushStyleColor(ImPlotCol_Line,
                                   ImVec4(0, 1, 1, 0.5f)); // Cyan for Min
            ImPlot::PlotLine("Min", plot->x_data.data(),
                             plot->y_data_min.data(),
                             static_cast<int>(plot->x_data.size()));
            ImPlot::PopStyleColor(3);

            ImPlot::EndPlot();
          }
        } else {
          ImGui::Dummy(ImVec2(-1, 80)); // Placeholder
        }
      } else {
        ImGui::Dummy(ImVec2(-1, 80)); // Placeholder
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sensor %d", i);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::End();
}