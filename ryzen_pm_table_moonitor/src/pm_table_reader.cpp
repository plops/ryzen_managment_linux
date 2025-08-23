#include <algorithm>
#include <array>
#include <cassert>
#include <span>
#include <cstring>
#include <spdlog/spdlog.h>
#include "pm_table_reader.hpp"
#include <fstream>

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
    std::vector<float> buffer(1024); // The file seems to be 4096 bytes, i.e., 1024 floats
    int bytes_to_read = buffer.size() * sizeof(float);
    bool first_read = true;

    while (running_) {
        std::ifstream pm_table_file(pm_table_path_, std::ios::binary);

        auto start_time = std::chrono::high_resolution_clock::now();

        // pm_table_file.seekg(0);
        pm_table_file.read(reinterpret_cast<char*>(buffer.data()), bytes_to_read);
        auto bytes_read = pm_table_file.gcount();
        spdlog::info("read {} bytes from PM table", bytes_read);
        if (first_read && (bytes_to_read != bytes_read)) {
            spdlog::warn("PMTableReader: Expected to read {} bytes, but read {} adjusting size", bytes_to_read, bytes_read);
            bytes_to_read = bytes_read; // Adjust to actual read size
            first_read = false;
        } else {
            spdlog::debug("PMTableReader: Successfully read {} bytes from PM table", bytes_read);
        }


        if (bytes_read > 0) {
            PMTableValues values = parse_pm_table_0x400005(buffer);
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

PMTableValues parse_pm_table_0x400005(const std::vector<float>& buffer) {
    PMTableValues v;
    // Offsets from pm_tables.c for 0x400005, only //o fields
    v.stapm_limit      = buffer.size() > 0   ? buffer[0]   : 0.0f;
    v.stapm_value      = buffer.size() > 1   ? buffer[1]   : 0.0f;
    v.ppt_limit_fast   = buffer.size() > 2   ? buffer[2]   : 0.0f;
    v.ppt_value_fast   = buffer.size() > 3   ? buffer[3]   : 0.0f;
    v.ppt_limit        = buffer.size() > 4   ? buffer[4]   : 0.0f;
    v.ppt_value        = buffer.size() > 5   ? buffer[5]   : 0.0f;
    v.ppt_limit_apu    = buffer.size() > 6   ? buffer[6]   : 0.0f;
    v.ppt_value_apu    = buffer.size() > 7   ? buffer[7]   : 0.0f;
    v.tdc_limit        = buffer.size() > 8   ? buffer[8]   : 0.0f;
    v.tdc_value        = buffer.size() > 9   ? buffer[9]   : 0.0f;
    v.tdc_limit_soc    = buffer.size() > 10  ? buffer[10]  : 0.0f;
    v.tdc_value_soc    = buffer.size() > 11  ? buffer[11]  : 0.0f;
    v.edc_limit        = buffer.size() > 12  ? buffer[12]  : 0.0f;
    v.edc_value        = buffer.size() > 13  ? buffer[13]  : 0.0f;
    v.thm_limit        = buffer.size() > 16  ? buffer[16]  : 0.0f;
    v.thm_value        = buffer.size() > 17  ? buffer[17]  : 0.0f;
    v.fit_limit        = buffer.size() > 26  ? buffer[26]  : 0.0f;
    v.fit_value        = buffer.size() > 27  ? buffer[27]  : 0.0f;
    v.vid_limit        = buffer.size() > 28  ? buffer[28]  : 0.0f;
    v.vid_value        = buffer.size() > 29  ? buffer[29]  : 0.0f;
    v.vddcr_cpu_power  = buffer.size() > 34  ? buffer[34]  : 0.0f;
    v.vddcr_soc_power  = buffer.size() > 35  ? buffer[35]  : 0.0f;
    v.socket_power     = buffer.size() > 38  ? buffer[38]  : 0.0f;
    v.package_power    = buffer.size() > 38  ? buffer[38]  : 0.0f;
    v.fclk_freq        = buffer.size() > 409 ? buffer[409] : 0.0f;
    v.fclk_freq_eff    = buffer.size() > 419 ? buffer[419] : 0.0f;
    v.uclk_freq        = buffer.size() > 410 ? buffer[410] : 0.0f;
    v.memclk_freq      = buffer.size() > 411 ? buffer[411] : 0.0f;
    v.soc_temp         = buffer.size() > 400 ? buffer[400] : 0.0f;
    v.peak_temp        = buffer.size() > 572 ? buffer[572] : 0.0f;
    v.peak_voltage     = buffer.size() > 573 ? buffer[573] : 0.0f;
    v.avg_core_count   = buffer.size() > 574 ? buffer[574] : 0.0f;
    v.cclk_limit       = buffer.size() > 0   ? 0.0f : 0.0f; // Not present in 0x400005, set to 0
    v.max_soc_voltage  = buffer.size() > 575 ? buffer[575] : 0.0f;
    v.prochot          = buffer.size() > 578 ? buffer[578] : 0.0f;
    v.pc6              = buffer.size() > 0   ? 0.0f : 0.0f; // Not present in 0x400005, set to 0
    v.gfx_voltage      = buffer.size() > 399 ? buffer[399] : 0.0f;
    v.gfx_temp         = buffer.size() > 400 ? buffer[400] : 0.0f;
    v.gfx_freq         = buffer.size() > 402 ? buffer[402] : 0.0f;
    v.gfx_busy         = buffer.size() > 404 ? buffer[404] : 0.0f;

    // Cores: 8 for 0x400005, offsets from pm_tables.c
    v.core_power.assign(buffer.begin() + 200, buffer.begin() + 208);
    v.core_voltage.assign(buffer.begin() + 208, buffer.begin() + 216);
    v.core_temp.assign(buffer.begin() + 216, buffer.begin() + 224);
    v.core_freq.assign(buffer.begin() + 240, buffer.begin() + 248);
    v.core_freq_eff.assign(buffer.begin() + 248, buffer.begin() + 256);
    v.core_c0.assign(buffer.begin() + 256, buffer.begin() + 264);
    v.core_cc1.assign(buffer.begin() + 264, buffer.begin() + 272);
    v.core_cc6.assign(buffer.begin() + 272, buffer.begin() + 280);

    return v;
}
