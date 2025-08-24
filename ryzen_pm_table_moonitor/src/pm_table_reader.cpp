#include "pm_table_reader.hpp"
#include <algorithm>
#include <fstream>
#include <span>
#include <spdlog/spdlog.h>
#include "jitter_monitor.hpp"

#include <sched.h>   // For scheduling policy functions
#include <cerrno>    // For errno
#include <cstring>   // For strerror

PMTableReader::PMTableReader(const std::string &path) : pm_table_path_(path) {}

void PMTableReader::start_reading() {
    spdlog::info("PMTableReader: Starting reading thread");
    running_       = true;
    reader_thread_ = std::thread(&PMTableReader::read_loop, this);

    // --- ENABLE REAL-TIME SCHEDULING ---

    // SCHED_FIFO is a real-time policy that runs threads to completion or until
    // they block or are preempted by a higher-priority thread.
    const int policy = SCHED_FIFO;

    // Set a high priority (1-99 for SCHED_FIFO).
    // Be careful not to starve critical system threads. 80 is a reasonably high value.
    sched_param params;
    params.sched_priority = 80;

    // pthread_setschedparam requires the native thread handle.
    // The first argument is the thread ID, the second is the policy, and the third
    // is a pointer to the scheduling parameters
    int ret = pthread_setschedparam(reader_thread_.native_handle(), policy, &params);
    if (ret != 0) {
        // Use strerror to get a human-readable error message.
        // A common error is EPERM (Operation not permitted), which means you
        // need to run the application with sufficient privileges (e.g., sudo).
        spdlog::error("PMTableReader: Failed to set thread scheduling policy. Error: {}", strerror(ret));
    } else {
        spdlog::info("PMTableReader: Successfully set thread scheduling policy to SCHED_FIFO with priority {}", params.sched_priority);
    }
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
    if (latest_data_.core_cc1.empty()) {
        return std::nullopt;
    }
    return latest_data_;
}


void PMTableReader::read_loop() {
    const std::chrono::microseconds target_period{1000};

    // --- 1. Initialization and First Read ---

    std::ifstream pm_table_file(pm_table_path_, std::ios::binary);
    if (!pm_table_file.is_open()) {
        spdlog::error("PMTableReader: Failed to open file: {}", pm_table_path_);
        return;
    }

    // Start with a reasonably large buffer.
    std::vector<float> buffer(1024);
    int bytes_to_read = buffer.size() * sizeof(float);

    // Perform the first read to determine the actual file size.
    pm_table_file.read(reinterpret_cast<char*>(buffer.data()), bytes_to_read);
    int bytes_read = pm_table_file.gcount();

    if (bytes_read <= 0) {
        spdlog::error("PMTableReader: Failed to read initial data from PM table. Exiting loop.");
        return;
    }

    // Now, adjust the read size and buffer for all subsequent reads.
    bytes_to_read = bytes_read;
    auto floats_to_read = bytes_to_read / sizeof(float);
    bytes_to_read = floats_to_read * sizeof(float); // Ensure it's a multiple of float size.
    buffer.resize(floats_to_read); // Truncate buffer to the exact size.
    spdlog::info("PMTableReader: Detected PM table size of {} bytes. Starting periodic reads.", bytes_to_read);

    // --- 2. Setup for Periodic Loop ---

    JitterMonitor jitter_monitor(target_period.count(), 5000 /* samples before reporting */, 2500);

    // Initialize timing *after* the first read is complete.
    auto next_wakeup = std::chrono::high_resolution_clock::now() + target_period;
    auto old_time = std::chrono::high_resolution_clock::now();

    // --- 3. Main Periodic Loop ---

    while (running_) {
        // Sleep until the next scheduled wakeup time.
        pm_table_file.clear(); // Clear EOF or other error flags
        pm_table_file.seekg(0, std::ios::beg);
        std::this_thread::sleep_until(next_wakeup);

        auto start_time = std::chrono::high_resolution_clock::now();

        // --- The Work ---

        // Read the PM table data from the proc file system. The kernel will initiate and transfer the measurement
        // from the SMU
        pm_table_file.read(reinterpret_cast<char*>(buffer.data()), bytes_to_read);

        bytes_read = pm_table_file.gcount();
        if (bytes_read > 0) {
            PMTableData new_data = parse_pm_table_0x400005(buffer);
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                latest_data_ = std::move(new_data);
            }
        } else {
            pm_table_file.clear(); // Clear EOF or other error flags
            spdlog::warn("PMTableReader: No data read from PM table");
        }

        // --- Jitter Measurement ---
        auto period = std::chrono::duration_cast<std::chrono::microseconds>(start_time - old_time);
        old_time = start_time;
        jitter_monitor.record_sample(period.count());

        // --- Schedule Next Wakeup ---
        next_wakeup += target_period;
    }
    spdlog::info("PMTableReader: Exiting read loop");
}

PMTableData parse_pm_table_0x400005(const std::vector<float> &buffer) {
    PMTableData v;
    // Offsets from pm_tables.c for 0x400005, only //o fields
    v.stapm_limit     = buffer.size() > 0 ? buffer[0] : 0.0f;
    v.stapm_value     = buffer.size() > 1 ? buffer[1] : 0.0f;
    v.ppt_limit_fast  = buffer.size() > 2 ? buffer[2] : 0.0f;
    v.ppt_value_fast  = buffer.size() > 3 ? buffer[3] : 0.0f;
    v.ppt_limit       = buffer.size() > 4 ? buffer[4] : 0.0f;
    v.ppt_value       = buffer.size() > 5 ? buffer[5] : 0.0f;
    v.ppt_limit_apu   = buffer.size() > 6 ? buffer[6] : 0.0f;
    v.ppt_value_apu   = buffer.size() > 7 ? buffer[7] : 0.0f;
    v.tdc_limit       = buffer.size() > 8 ? buffer[8] : 0.0f;
    v.tdc_value       = buffer.size() > 9 ? buffer[9] : 0.0f;
    v.tdc_limit_soc   = buffer.size() > 10 ? buffer[10] : 0.0f;
    v.tdc_value_soc   = buffer.size() > 11 ? buffer[11] : 0.0f;
    v.edc_limit       = buffer.size() > 12 ? buffer[12] : 0.0f;
    v.edc_value       = buffer.size() > 13 ? buffer[13] : 0.0f;
    v.thm_limit       = buffer.size() > 16 ? buffer[16] : 0.0f;
    v.thm_value       = buffer.size() > 17 ? buffer[17] : 0.0f;
    v.fit_limit       = buffer.size() > 26 ? buffer[26] : 0.0f;
    v.fit_value       = buffer.size() > 27 ? buffer[27] : 0.0f;
    v.vid_limit       = buffer.size() > 28 ? buffer[28] : 0.0f;
    v.vid_value       = buffer.size() > 29 ? buffer[29] : 0.0f;
    v.vddcr_cpu_power = buffer.size() > 34 ? buffer[34] : 0.0f;
    v.vddcr_soc_power = buffer.size() > 35 ? buffer[35] : 0.0f;
    v.socket_power    = buffer.size() > 38 ? buffer[38] : 0.0f;
    v.package_power   = buffer.size() > 38 ? buffer[38] : 0.0f;
    v.fclk_freq       = buffer.size() > 409 ? buffer[409] : 0.0f;
    v.fclk_freq_eff   = buffer.size() > 419 ? buffer[419] : 0.0f;
    v.uclk_freq       = buffer.size() > 410 ? buffer[410] : 0.0f;
    v.memclk_freq     = buffer.size() > 411 ? buffer[411] : 0.0f;
    v.soc_temp        = buffer.size() > 400 ? buffer[400] : 0.0f;
    v.peak_temp       = buffer.size() > 572 ? buffer[572] : 0.0f;
    v.peak_voltage    = buffer.size() > 573 ? buffer[573] : 0.0f;
    v.avg_core_count  = buffer.size() > 574 ? buffer[574] : 0.0f;
    v.cclk_limit      = buffer.size() > 0 ? 0.0f : 0.0f; // Not present in 0x400005, set to 0
    v.max_soc_voltage = buffer.size() > 575 ? buffer[575] : 0.0f;
    v.prochot         = buffer.size() > 578 ? buffer[578] : 0.0f;
    v.pc6             = buffer.size() > 0 ? 0.0f : 0.0f; // Not present in 0x400005, set to 0
    v.gfx_voltage     = buffer.size() > 399 ? buffer[399] : 0.0f;
    v.gfx_temp        = buffer.size() > 400 ? buffer[400] : 0.0f;
    v.gfx_freq        = buffer.size() > 402 ? buffer[402] : 0.0f;
    v.gfx_busy        = buffer.size() > 404 ? buffer[404] : 0.0f;

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
