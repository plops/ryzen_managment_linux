#include "pm_table_reader.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <spdlog/spdlog.h>

PMTableReader::PMTableReader(const std::string& path) : pm_table_path_(path) {}

void PMTableReader::start_reading() {
    spdlog::info("PMTableReader: Starting reading thread");
    running_ = true;
    reader_thread_ = std::thread(&PMTableReader::read_loop, this);
}

void PMTableReader::stop_reading() {
    spdlog::info("PMTableReader: Stopping reading thread");
    running_ = false;
    if (reader_thread_.joinable()) {
        reader_thread_.join();
        spdlog::info("PMTableReader: Reading thread stopped");
    }
}

std::optional<PMTableData> PMTableReader::get_latest_data() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (latest_data_.core_clocks.empty()) {
        return std::nullopt;
    }
    return latest_data_;
}

void PMTableReader::read_loop() {
    std::ifstream pm_table_file(pm_table_path_, std::ios::binary);
    if (!pm_table_file) {
        spdlog::error("PMTableReader: Could not open {}", pm_table_path_);
        return;
    }
    spdlog::info("PMTableReader: Opened {}", pm_table_path_);

    std::vector<float> buffer(4096); // Adjust buffer size as needed

    while (running_) {
        auto start_time = std::chrono::high_resolution_clock::now();

        pm_table_file.seekg(0);
        pm_table_file.read(reinterpret_cast<char*>(buffer.data()), buffer.size() * sizeof(float));

        if (pm_table_file.gcount() > 0) {
            PMTableData new_data;
            // Example: reading 8 core clocks starting at a hypothetical offset of 10
            new_data.core_clocks.assign(buffer.begin() + 10, buffer.begin() + 18);
            // Example: reading 8 core powers starting at a hypothetical offset of 20
            new_data.core_powers.assign(buffer.begin() + 20, buffer.begin() + 28);
            // Example: reading package power at a hypothetical offset of 30
            new_data.package_power = buffer[30];

            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                latest_data_ = std::move(new_data);
            }
            spdlog::debug("PMTableReader: Read new data from PM table");
        } else {
            spdlog::warn("PMTableReader: No data read from PM table");
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        auto sleep_time = std::chrono::microseconds(1000) - duration;
        if (sleep_time.count() > 0) {
            std::this_thread::sleep_for(sleep_time);
        }
    }
    spdlog::info("PMTableReader: Exiting read loop");
}