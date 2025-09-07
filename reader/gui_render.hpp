#pragma once

#include "shared_data_types.hpp"
#include <atomic>
#include <string>
#include <vector>

void render_gui(
    const std::vector<std::atomic<DisplayData *>> &gui_display_pointers,
    int n_total_sensors, const std::vector<int> &interesting_indices,
    const std::string &experiment_status, CommandQueue &command_queue,
    std::atomic<bool> &manual_mode, std::atomic<int> &manual_core_to_test,
    int num_hardware_threads);