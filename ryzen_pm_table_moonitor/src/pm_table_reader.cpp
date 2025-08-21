#include "pm_table_reader.hpp"
#include <fstream>
#include <iostream>
#include <chrono>

PMTableReader::PMTableReader(const std::string& path) : pm_table_path_(path) {}

void PMTableReader::start_reading() {
    running_ = true;
    reader_thread_ = std::thread(&PMTableReader::read_loop, this);
}

void PMTableReader::stop_reading() {
    running_ = false;
    if (reader_thread_.joinable()) {
        reader_thread_.join();
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
        std::cerr << "Error: Could not open " << pm_table_path_ << std::endl;
        return;
    }

    std::vector<float> buffer(4096); // Adjust buffer size as needed

    while (running_) {
        auto start_time = std::chrono::high_resolution_clock::now();

        pm_table_file.seekg(0);
        pm_table_file.read(reinterpret_cast<char*>(buffer.data()), buffer.size() * sizeof(float));

        if (pm_table_file.gcount() > 0) {
            // This is a simplified example. The actual offsets for the data
            // you want to read from the pm_table will depend on your specific
            // CPU and the ryzen_smu driver version.
            // You will need to consult the ryzen_smu documentation or other
            // resources to find the correct offsets.
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
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        auto sleep_time = std::chrono::microseconds(1000) - duration;
        if (sleep_time.count() > 0) {
            std::this_thread::sleep_for(sleep_time);
        }
    }
}