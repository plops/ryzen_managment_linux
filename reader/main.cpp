// main.cpp

#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <stdexcept>
#include <cstdint>
#include <unistd.h> // For geteuid

// --- Configuration ---
const char* PM_TABLE_PATH = "/sys/kernel/ryzen_smu_drv/pm_table";
const char* PM_TABLE_SIZE_PATH = "/sys/kernel/ryzen_smu_drv/pm_table_size";
const char* OUTPUT_FILE_PATH = "pm_table_log.bin";
// The target sampling period of 1 millisecond (1kHz)
constexpr auto SAMPLING_PERIOD = std::chrono::milliseconds(1);

// --- Signal Handling for graceful shutdown (Ctrl+C) ---
std::atomic<bool> running = true;

void signalHandler(int signum) {
    std::cout << "\nCaught signal " << signum << ". Shutting down..." << std::endl;
    running = false;
}

/**
 * @brief Reads a 64-bit unsigned integer from a sysfs file.
 * @param path The path to the sysfs file.
 * @return The uint64_t value read from the file.
 * @throws std::runtime_error if the file cannot be opened or read.
 */
uint64_t read_sysfs_uint64(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open sysfs file: " + path);
    }
    uint64_t value = 0;
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!file) {
        throw std::runtime_error("Failed to read from sysfs file: " + path);
    }
    return value;
}
#define SPDLOG_ERROR
#define SPDLOG_INFO
#define SPDLOG_WARN

void setup() {
    // --- ENABLE REAL-TIME SCHEDULING ---
    const int policy = SCHED_FIFO;
    sched_param params{};
    // Set a high priority (1-99 for SCHED_FIFO).
    params.sched_priority = 80;

    auto current_thread = pthread_self();

    int ret = pthread_setschedparam(current_thread, policy, &params);
    // if (ret != 0) {
    //     SPDLOG_ERROR("Failed to set thread scheduling policy for worker {}. Error: {}", worker.id(), strerror(ret));
    //     SPDLOG_WARN("You may need to run with sudo or grant CAP_SYS_NICE capabilities.");
    // } else {
    //     SPDLOG_INFO("Successfully set worker {} scheduling policy to SCHED_FIFO with priority {}", worker.id(), params.sched_priority);
    // }

    // --- SET CPU AFFINITY ---
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    // Pin the thread to a specific core, e.g., core 3
    const int core_id = 0;
    CPU_SET(core_id, &cpuset);

    ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    // if (ret != 0) {
    //     SPDLOG_ERROR("Failed to set CPU affinity for worker {}. Error: {}", worker.id(), strerror(ret));
    // } else {
    //     SPDLOG_INFO("Successfully pinned worker {} to CPU {}", worker.id(), core_id);
    // }
}

int main() {
    // Register signal handlers for SIGINT (Ctrl+C) and SIGTERM
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    setup();
    // Check for root privileges, which are required to access the driver's sysfs files
    // if (geteuid() != 0) {
    //     std::cerr << "Error: This program requires root privileges to read from sysfs." << std::endl;
    //     std::cerr << "Please run with sudo." << std::endl;
    //     return EXIT_FAILURE;
    // }


    try {
        // First, determine the exact size of the pm_table
        uint64_t pm_table_size = read_sysfs_uint64(PM_TABLE_SIZE_PATH);
        if (pm_table_size == 0 || pm_table_size > 16384) { // Sanity check size
             std::cerr << "Error: Invalid pm_table size reported: " << pm_table_size << " bytes." << std::endl;
             return EXIT_FAILURE;
        }
        std::cout << "Detected pm_table size: " << pm_table_size << " bytes." << std::endl;

        // Open the pm_table file for reading
        std::ifstream pm_table_stream(PM_TABLE_PATH, std::ios::binary);
        if (!pm_table_stream) {
            std::cerr << "Error: Failed to open " << PM_TABLE_PATH << "." << std::endl;
            std::cerr << "Is the ryzen_smu kernel module loaded?" << std::endl;
            return EXIT_FAILURE;
        }
        pm_table_stream.seekg(0); // Seek to the beginning for each read


        // Open the output file for writing the logged data
        std::ofstream output_stream(OUTPUT_FILE_PATH, std::ios::binary | std::ios::trunc);
        if (!output_stream) {
            std::cerr << "Error: Failed to open output file " << OUTPUT_FILE_PATH << " for writing." << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << "Starting to read pm_table at 1kHz. Press Ctrl+C to stop." << std::endl;
        std::cout << "Writing data to " << OUTPUT_FILE_PATH << std::endl;

        std::vector<char> buffer(pm_table_size);
        uint64_t samples_written = 0;

        // --- The Main High-Precision Loop ---
        auto next_sample_time = std::chrono::steady_clock::now();

        while (running) {
            // Calculate the time for the next iteration to maintain a consistent 1kHz rate
            next_sample_time += SAMPLING_PERIOD;

            // 1. Get a high-resolution timestamp (nanoseconds since epoch)
            auto timestamp = std::chrono::system_clock::now().time_since_epoch();
            auto timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp).count();
            uint64_t timestamp_u64 = static_cast<uint64_t>(timestamp_ns);

            // 2. Read the binary pm_table data
            pm_table_stream.read(buffer.data(), pm_table_size);

            if (!pm_table_stream) {
                 std::cerr << "\nWarning: Failed to read from " << PM_TABLE_PATH << " on sample " << samples_written << std::endl;
                 // Don't write partial data; just wait for the next cycle
                 std::this_thread::sleep_until(next_sample_time);
                 continue;
            }

            // 3. Write the timestamp, data size, and data to the output file
            output_stream.write(reinterpret_cast<const char*>(&timestamp_u64), sizeof(timestamp_u64));
            output_stream.write(reinterpret_cast<const char*>(&pm_table_size), sizeof(pm_table_size));
            output_stream.write(buffer.data(), pm_table_size);

            samples_written++;

            pm_table_stream.seekg(0); // Seek to the beginning for each read

            // 4. Sleep until the next scheduled sample time
            std::this_thread::sleep_until(next_sample_time);
        }

        // --- Cleanup ---
        std::cout << "Stopped. Wrote " << samples_written << " samples to " << OUTPUT_FILE_PATH << "." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "A critical error occurred: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}