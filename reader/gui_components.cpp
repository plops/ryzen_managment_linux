#include "gui_components.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <vector>

void GuiDataCache::update(const EyeDiagramStorage &eye_storage) {
    // This lock protects our internal plot_data.
    std::lock_guard<std::mutex> lock(data_mutex);

    // The EyeDiagramStorage is now a snapshot provided by the GuiRunner,
    // so we no longer need to lock it here.
    // std::lock_guard<std::mutex> eye_lock(eye_storage.storage_mutex); // REMOVED

    size_t n_interesting_sensors = eye_storage.bins.size();
    if (plot_data.size() != n_interesting_sensors) {
        plot_data.resize(n_interesting_sensors);
    }

    for (size_t i = 0; i < n_interesting_sensors; ++i) {
        plot_data[i].original_sensor_index = eye_storage.original_sensor_indices[i];
        plot_data[i].x_data.clear();
        plot_data[i].y_data.clear();

        for (int bin_idx = 0; bin_idx < eye_storage.num_bins; ++bin_idx) {
            const auto &bin = eye_storage.bins[i][bin_idx];
            if (!bin.empty()) {
                // Simplified median calculation
                std::vector<float> sorted_bin = bin;
                std::sort(sorted_bin.begin(), sorted_bin.end());
                float median;
                size_t n = sorted_bin.size();
                if (n % 2 == 0) {
                    median = (sorted_bin[n / 2 - 1] + sorted_bin[n / 2]) / 2.0f;
                } else {
                    median = sorted_bin[n / 2];
                }

                plot_data[i].x_data.push_back(static_cast<float>(bin_idx - eye_storage.zero_offset_bins));
                plot_data[i].y_data.push_back(median);
            }
        }
    }
}

void render_gui(GuiDataCache &cache, int n_total_sensors, const std::vector<int>& interesting_indices, const std::string& experiment_status) {
    ImGui::Begin("PM Table Eye Diagrams");
    ImGui::Text("Experiment Status: %s", experiment_status.c_str());
    ImGui::Separator();

    const int num_columns = 16;
    if (ImGui::BeginTable("EyeDiagramGrid", num_columns, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        for (int i = 0; i < n_total_sensors; ++i) {
            if (i % num_columns == 0) ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(i % num_columns);

            auto it = std::find(interesting_indices.begin(), interesting_indices.end(), i);
            bool is_interesting = it != interesting_indices.end();

            ImGui::PushID(i);
            if (is_interesting) {
                size_t cache_idx = std::distance(interesting_indices.begin(), it);

                // Lock the cache just for the brief moment we access the plot data.
                std::lock_guard<std::mutex> lock(cache.data_mutex);
                if (cache_idx < cache.plot_data.size() && !cache.plot_data[cache_idx].x_data.empty()) {
                    const auto& plot = cache.plot_data[cache_idx];

                    // Use a small plot for the grid cell
                    if (ImPlot::BeginPlot("##EyePlot", ImVec2(100, 60), ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs)) {
                        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
                        ImPlot::PlotLine("Median", plot.x_data.data(), plot.y_data.data(), static_cast<int>(plot.x_data.size()));
                        ImPlot::EndPlot();
                    }
                } else {
                     // Black box if no data yet
                    ImGui::ColorButton("##empty", ImVec4(0,0,0,1), ImGuiColorEditFlags_NoTooltip, ImVec2(100, 60));
                }

            } else {
                // Non-changing values are just black areas
                ImGui::ColorButton("##non_interesting", ImVec4(0,0,0,1), ImGuiColorEditFlags_NoTooltip, ImVec2(100, 60));
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Sensor %d", i);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
