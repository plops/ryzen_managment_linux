// start of pm_table_reader.hpp
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>
// NO LONGER INCLUDES analysis_manager.hpp, it's now decoupled.


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
    // Path is now optional in the constructor
    explicit PMTableReader(const std::string &path = "/sys/kernel/ryzen_smu_drv/pm_table");

    // The main loop is gone. No start/stop.

    std::optional<PMTableData> get_latest_data();

private:
    // These members are no longer needed for the pipeline,
    // but we keep latest_data_ and its mutex for the "Decoded View" tab
    // which is not part of the high-frequency analysis path.
    std::string pm_table_path_;
    std::mutex  data_mutex_;
    PMTableData latest_data_;

    // Friend declaration to allow main pipeline to access private members
    friend int main();
};
