#pragma once
#include <string>
#include <fstream>
#include <cstdint>

class PmTableReader {
public:
    PmTableReader();
    uint64_t getPmTableSize() const;
    void read(char *buffer); // reads pm_table_size bytes into buffer

private:
    uint64_t read_sysfs_uint64(const std::string &path);
    uint64_t pm_table_size;
    std::ifstream pm_table_stream;
};

