#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <optional>

struct PMTableData {
    std::vector<float> core_clocks;
    std::vector<float> core_powers;
    float package_power;
};

class PMTableReader {
public:
    PMTableReader(const std::string& path = "/sys/kernel/ryzen_smu_drv/pm_table");
    void start_reading();
    void stop_reading();
    std::optional<PMTableData> get_latest_data();

private:
    void read_loop();

    std::string pm_table_path_;
    bool running_ = false;
    std::thread reader_thread_;
    std::mutex data_mutex_;
    PMTableData latest_data_;
};