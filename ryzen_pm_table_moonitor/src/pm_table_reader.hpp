#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "analysis_manager.hpp"

struct PMTableData {
    // Only fields marked with //o in pm_tables.c, using 0x400005 as reference
    float stapm_limit     = 0.0f;
    float stapm_value     = 0.0f;
    float ppt_limit_fast  = 0.0f;
    float ppt_value_fast  = 0.0f;
    float ppt_limit       = 0.0f;
    float ppt_value       = 0.0f;
    float ppt_limit_apu   = 0.0f;
    float ppt_value_apu   = 0.0f;
    float tdc_limit       = 0.0f;
    float tdc_value       = 0.0f;
    float tdc_limit_soc   = 0.0f;
    float tdc_value_soc   = 0.0f;
    float edc_limit       = 0.0f;
    float edc_value       = 0.0f;
    float thm_limit       = 0.0f;
    float thm_value       = 0.0f;
    float fit_limit       = 0.0f;
    float fit_value       = 0.0f;
    float vid_limit       = 0.0f;
    float vid_value       = 0.0f;
    float vddcr_cpu_power = 0.0f;
    float vddcr_soc_power = 0.0f;
    float socket_power    = 0.0f;
    float package_power   = 0.0f;
    float fclk_freq       = 0.0f;
    float fclk_freq_eff   = 0.0f;
    float uclk_freq       = 0.0f;
    float memclk_freq     = 0.0f;
    float soc_temp        = 0.0f;
    float peak_temp       = 0.0f;
    float peak_voltage    = 0.0f;
    float avg_core_count  = 0.0f;
    float cclk_limit      = 0.0f;
    float max_soc_voltage = 0.0f;
    float prochot         = 0.0f;
    float pc6             = 0.0f;
    float gfx_voltage     = 0.0f;
    float gfx_temp        = 0.0f;
    float gfx_freq        = 0.0f;
    float gfx_busy        = 0.0f;

    std::vector<float> core_power;
    std::vector<float> core_voltage;
    std::vector<float> core_temp;
    std::vector<float> core_freq;
    std::vector<float> core_freq_eff;
    std::vector<float> core_c0;
    std::vector<float> core_cc1;
    std::vector<float> core_cc6;
    // Add more fields as needed
};

PMTableData parse_pm_table_0x400005(const std::vector<float> &buffer);

class PMTableReader {
public:
    explicit PMTableReader(AnalysisManager& manager, const std::string &path = "/sys/kernel/ryzen_smu_drv/pm_table");
    void                       start_reading();
    void                       stop_reading();
    std::optional<PMTableData> get_latest_data();

private:
    void read_loop();

    std::string pm_table_path_;
    std::atomic<bool> running_{false};
    std::thread reader_thread_;
    std::mutex  data_mutex_;
    PMTableData latest_data_;
    AnalysisManager& analysis_manager_;
};