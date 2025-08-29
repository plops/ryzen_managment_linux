#pragma once
#include <string>
#include <fstream>
#include <cstdint>

/**
 * @class PmTableReader
 * @brief Simple helper that reads the kernel pm_table sysfs blob and reports its size.
 *
 * The class opens /sys/kernel/ryzen_smu_drv/pm_table and reads pm_table_size bytes on demand.
 */
class PmTableReader {
public:
    /** @brief Construct and open the pm_table and read pm_table_size from sysfs. */
    PmTableReader();

    /**
     * @brief Get the pm_table size in bytes.
     * @return Size in bytes as reported by /sys/kernel/ryzen_smu_drv/pm_table_size.
     */
    uint64_t getPmTableSize() const;

    /**
     * @brief Read pm_table_size bytes into the provided buffer.
     *
     * The buffer must be at least getPmTableSize() bytes long.
     *
     * @param buffer Destination buffer.
     */
    void read(char *buffer); // reads pm_table_size bytes into buffer

private:
    uint64_t read_sysfs_uint64(const std::string &path);
    uint64_t pm_table_size;
    std::ifstream pm_table_stream;
};
