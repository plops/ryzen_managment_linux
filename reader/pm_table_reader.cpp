#include "pm_table_reader.hpp"
#include <iostream>
#include <stdexcept>
#include <spdlog/spdlog.h>
/**
 * @brief Construct the reader by querying sysfs and opening the pm_table file.
 *
 * Throws runtime_error on failures reading sysfs values.
 */
PmTableReader::PmTableReader()
    : pm_table_size{read_sysfs_uint64("/sys/kernel/ryzen_smu_drv/pm_table_size")},
      pm_table_stream("/sys/kernel/ryzen_smu_drv/pm_table", std::ios::binary) {
    if (pm_table_size == 0 || pm_table_size > 16384) {
        SPDLOG_ERROR("Invalid pm_table size reported: {} bytes.", pm_table_size);
    }
    SPDLOG_TRACE("Detected pm_table size: {} bytes.", pm_table_size);
    if (!pm_table_stream) {
        SPDLOG_ERROR("Failed to open /sys/kernel/ryzen_smu_drv/pm_table.");
    }
    pm_table_stream.seekg(0);
}

/**
 * @brief Return the pm_table size previously read from sysfs.
 */
uint64_t PmTableReader::getPmTableSize() const {
    return pm_table_size;
}

/**
 * @brief Read the pm_table blob into a caller-supplied buffer.
 *
 * This method reads pm_table_size bytes and rewinds the stream to the start.
 */
void PmTableReader::read(char *buffer) {
    pm_table_stream.read(buffer, getPmTableSize());
    pm_table_stream.seekg(0);
}

/**
 * @brief Read a uint64 value from a sysfs file.
 *
 * @param path Path to the sysfs file.
 * @return Parsed uint64 value.
 */
uint64_t PmTableReader::read_sysfs_uint64(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open sysfs file: " + path);
    }
    uint64_t value = 0;
    file.read(reinterpret_cast<char *>(&value), sizeof(value));
    if (!file) {
        throw std::runtime_error("Failed to read from sysfs file: " + path);
    }
    return value;
}
