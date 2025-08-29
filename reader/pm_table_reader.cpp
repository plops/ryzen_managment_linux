#include "pm_table_reader.hpp"
#include <iostream>
#include <stdexcept>

PmTableReader::PmTableReader()
    : pm_table_size{read_sysfs_uint64("/sys/kernel/ryzen_smu_drv/pm_table_size")},
      pm_table_stream("/sys/kernel/ryzen_smu_drv/pm_table", std::ios::binary) {
    if (pm_table_size == 0 || pm_table_size > 16384) {
        std::cerr << "Error: Invalid pm_table size reported: " << pm_table_size << " bytes." << std::endl;
    }
    std::cout << "Detected pm_table size: " << pm_table_size << " bytes." << std::endl;
    if (!pm_table_stream) {
        std::cerr << "Error: Failed to open /sys/kernel/ryzen_smu_drv/pm_table." << std::endl;
    }
    pm_table_stream.seekg(0);
}

uint64_t PmTableReader::getPmTableSize() const {
    return pm_table_size;
}

void PmTableReader::read(char *buffer) {
    pm_table_stream.read(buffer, getPmTableSize());
    pm_table_stream.seekg(0);
}

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

