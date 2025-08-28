// measure.cpp
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <memory>
#include <cassert>

#include "popl.hpp"
#include "workloads.hpp"

// Use steady_clock for monotonic time measurements
using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = std::chrono::duration<double, std::nano>;

// --- Data Structures for Pre-allocated Storage ---

// Represents a single sample from the measurement thread
struct MeasurementSample {
    explicit MeasurementSample(size_t n_measurements) : measurements(n_measurements) {
    };
    TimePoint timestamp{};
    int worker_state{0}; // 0 for waiting, 1 for busy
    std::vector<float> measurements;
    std::vector<uint64_t> eye_off0;
    std::vector<uint64_t> eye_on;
    std::vector<uint64_t> eye_off1;
};

// Represents a state transition event from the worker thread
struct WorkerTransition {
    TimePoint timestamp;
    int new_state{}; // 0 for waiting, 1 for busy
};

// NEW: Structure to hold the binned data for an eye diagram.
// This acts as a histogram where each bin corresponds to a 1ms time slot
// relative to a rising edge event.
struct EyeDiagramStorage {
    // --- Configuration ---
    static constexpr int WINDOW_BEFORE_MS = 50;  // How many ms to look back before the edge
    static constexpr int WINDOW_AFTER_MS = 150; // How many ms to capture after the edge
    static constexpr int NUM_BINS = WINDOW_BEFORE_MS + WINDOW_AFTER_MS;
    // The index in our bins array that corresponds to t=0 (the rising edge)
    static constexpr int ZERO_OFFSET_BINS = WINDOW_BEFORE_MS;

    // --- Storage ---
    // A vector of vectors. The outer vector represents the time bins.
    // The inner vector stores all sensor values that fell into that time bin
    // across all detected rising edge events.
    std::vector<std::vector<float>> bins;
    size_t event_count{0}; // How many rising edge events were captured

    // --- Methods ---
    EyeDiagramStorage() : bins(NUM_BINS) {
        // Pre-allocate some space in the inner vectors to reduce reallocations during measurement
        for(auto& bin : bins) {
            bin.reserve(100); // Reserve space for 100 cycles/events
        }
    }

    // Reset the storage for a new run
    void clear() {
        event_count = 0;
        for (auto& bin : bins) {
            bin.clear();
        }
    }
};

// --- PM Table Reader ---

/** @brief Reads float array from sysfs pm_table
 *  Constructor detects the size of the pm_table
 *  read reads all the elements into a char pointer and seeks back for next iteration
 */
class PmTableReader {
    const char *PM_TABLE_PATH = "/sys/kernel/ryzen_smu_drv/pm_table";
    const char *PM_TABLE_SIZE_PATH = "/sys/kernel/ryzen_smu_drv/pm_table_size";

public:
    PmTableReader()
        : pm_table_size{read_sysfs_uint64(PM_TABLE_SIZE_PATH)}, // First, determine the exact size of the pm_table
          pm_table_stream(PM_TABLE_PATH, std::ios::binary) {
        if (pm_table_size == 0 || pm_table_size > 16384) {
            // Sanity check size
            std::cerr << "Error: Invalid pm_table size reported: " << pm_table_size << " bytes." << std::endl;
        }
        std::cout << "Detected pm_table size: " << pm_table_size << " bytes." << std::endl;
        if (!pm_table_stream) {
            std::cerr << "Error: Failed to open " << PM_TABLE_PATH << "." << std::endl;
            std::cerr << "Is the ryzen_smu kernel module loaded?" << std::endl;
        }
        pm_table_stream.seekg(0); // Seek to the beginning for each read
    }

    uint64_t getPmTableSize() const {
        return pm_table_size;
    }

    void read(char *buffer) {
        pm_table_stream.read(buffer, getPmTableSize());
        pm_table_stream.seekg(0); // Seek to the beginning for next read
    }

private:
    /**
    * @brief Reads a 64-bit unsigned integer from a sysfs file.
    * @param path The path to the sysfs file.
    * @return The uint64_t value read from the file.
    * @throws std::runtime_error if the file cannot be opened or read.
    */
    uint64_t read_sysfs_uint64(const std::string &path) {
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

    uint64_t pm_table_size;
    std::ifstream pm_table_stream;
};

// --- Global Atomic Flags for Thread Synchronization ---
// These are used to signal start/stop without using mutexes
std::atomic<bool> g_run_measurement = false;
std::atomic<bool> g_run_worker = false;
std::atomic<int> g_worker_state = 0; // The single point of communication during a run

/*
 * a state machine to help capture the eye diagram
 * the internal states are (at least these active states): off_0, on, off_2
 * in these active states we bin the incoming data into
 * everytime g_workers state transitions from 0 to 1 we
 * will store into storage[].measurements.eye_{off0,on,off1}
 * these storage containers act like a 2D histogram, containing
 * the number of observed measurements at each 1ms
 */

// --- Thread Functions ---

void measurement_thread_func(int core_id,
                             std::vector<MeasurementSample> &storage,
                             size_t &sample_count,
                             PmTableReader &pm_table_reader,
                             EyeDiagramStorage &eye_storage) { // NEW: Pass eye storage by reference
    if (!set_thread_affinity(core_id)) {
        std::cerr << "Warning: Failed to set measurement thread affinity to core " << core_id << std::endl;
    }

    // Wait for the signal to start
    while (!g_run_measurement.load(std::memory_order_acquire));

    const auto sample_period = std::chrono::milliseconds(1);
    auto next_sample_time = Clock::now();
    sample_count = 0;

    // --- NEW: State machine and variables for eye diagram capture ---
    enum class CaptureState { IDLE, CAPTURING };
    CaptureState capture_state = CaptureState::IDLE;
    int last_worker_state = 0;
    TimePoint last_rise_time;
    const int v17_sensor_index = 17; // The index of our target sensor

    while (g_run_measurement.load(std::memory_order_acquire)) {
        // Record timestamp and state immediately
        TimePoint timestamp = Clock::now();
        int worker_state = g_worker_state.load(std::memory_order_relaxed);

        // Store in pre-allocated buffer
        if (sample_count < storage.size()) {
            auto& target = storage[sample_count];
            auto floatPtr = target.measurements.data();
            auto charPtr = reinterpret_cast<char *>(floatPtr);
            // Read sensors
            pm_table_reader.read(charPtr);
            target.timestamp = timestamp;
            target.worker_state = worker_state;
            sample_count++;
        }
        assert(sample_count < storage.size());

        // --- START OF DEAD TIME COMPUTATION ---
        // This is where we perform the regridding/binning.

        // 1. State Machine: Detect rising edge (0 -> 1 transition)
        if (worker_state == 1 && last_worker_state == 0) {
            // Rising edge detected! Start a new capture window.
            capture_state = CaptureState::CAPTURING;
            last_rise_time = timestamp;
            eye_storage.event_count++;
        }
        last_worker_state = worker_state;

        // 2. Binning: If we are in a capture window, bin this sample.
        if (capture_state == CaptureState::CAPTURING) {
            // Calculate time difference in milliseconds from the last rising edge
            auto time_since_rise = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp - last_rise_time);
            long long time_delta_ms = time_since_rise.count();

            // Convert the relative time into an index into our storage array
            long long bin_index = time_delta_ms + EyeDiagramStorage::ZERO_OFFSET_BINS;

            // Check if this sample falls within our defined window [-50ms, +150ms]
            if (bin_index >= 0 && bin_index < EyeDiagramStorage::NUM_BINS) {
                // It's in range, so store the sensor value in the correct time bin
                const float v17_value = storage[sample_count - 1].measurements[v17_sensor_index];
                eye_storage.bins[bin_index].push_back(v17_value);
            } else if (bin_index >= EyeDiagramStorage::NUM_BINS) {
                // We have moved past the capture window for this event, reset to idle.
                capture_state = CaptureState::IDLE;
            }
        }
        // --- END OF DEAD TIME COMPUTATION ---

        // Wait until the next 1ms interval
        next_sample_time += sample_period;
        std::this_thread::sleep_until(next_sample_time);
    }
}

void worker_thread_func(int core_id,
                        int period_ms,
                        int duty_cycle_percent,
                        int num_cycles,
                        std::vector<WorkerTransition> &storage,
                        size_t &transition_count) {
    if (!set_thread_affinity(core_id)) {
        std::cerr << "Warning: Failed to set worker thread affinity to core " << core_id << std::endl;
    }

    const auto period = std::chrono::milliseconds(period_ms);
    const auto busy_duration = period * duty_cycle_percent / 100;
    const auto wait_duration = period - busy_duration;
    transition_count = 0;

    // Wait for the signal to start
    while (!g_run_worker.load(std::memory_order_acquire));

    for (int i = 0; i < num_cycles; ++i) {
        // --- BUSY PHASE ---
        g_worker_state.store(1, std::memory_order_relaxed);
        if (transition_count < storage.size()) {
            storage[transition_count++] = {Clock::now(), 1};
        }
        assert(transition_count < storage.size());

        auto busy_start = Clock::now();
        while ((Clock::now() - busy_start) < busy_duration) {
            integer_alu_workload(1000); // Inner loop to keep CPU busy
        }

        // --- WAITING PHASE ---
        g_worker_state.store(0, std::memory_order_relaxed);
        if (transition_count < storage.size()) {
            storage[transition_count++] = {Clock::now(), 0};
        }

        std::this_thread::sleep_for(wait_duration);
    }
}


// --- Analysis Function ---
void analyze_and_print_results(int core_id,
                               const std::vector<MeasurementSample> &measurements, size_t sample_count,
                               const std::vector<WorkerTransition> &transitions, size_t transition_count,
                               const EyeDiagramStorage &eye_storage) { // NEW: Pass eye storage
    std::cout << "--- Analyzing Core " << core_id << " ---" << std::endl;
    std::cout << "Collected " << sample_count << " measurement samples." << std::endl;
    std::cout << "Recorded " << transition_count << " worker transitions." << std::endl;

    std::vector<double> busy_samples;
    std::vector<double> wait_samples;

    for (size_t i = 0; i < sample_count; ++i) {
        if (measurements[i].worker_state == 1) {
            busy_samples.push_back(measurements[i].measurements[17]); // THM VALUE
        } else {
            wait_samples.push_back(measurements[i].measurements[17]);
        }
    }

    if (busy_samples.empty() || wait_samples.empty()) {
        std::cout << "Not enough data to compute statistics." << std::endl;
        return;
    }

    double busy_mean = std::accumulate(busy_samples.begin(), busy_samples.end(), 0.0) / busy_samples.size();
    double wait_mean = std::accumulate(wait_samples.begin(), wait_samples.end(), 0.0) / wait_samples.size();

    std::cout << "Mean THM value while BUSY:   " << busy_mean << std::endl;
    std::cout << "Mean THM value while WAITING: " << wait_mean << std::endl;
    std::cout << "Mean Correlation (Difference): " << busy_mean - wait_mean << std::endl;

    // --- NEW: Eye Diagram Median Calculation ---
    std::cout << "\n--- Eye Diagram Median Trace (v17) ---" << std::endl;
    std::cout << "Captured " << eye_storage.event_count << " rising edge events." << std::endl;
    std::cout << "Time(ms)\tMedian\tSamples" << std::endl;

    for (int i = 0; i < EyeDiagramStorage::NUM_BINS; ++i) {
        const auto& bin = eye_storage.bins[i];
        if (!bin.empty()) {
            // Calculate the time relative to the rising edge for this bin
            int relative_time_ms = i - EyeDiagramStorage::ZERO_OFFSET_BINS;

            // To find the median, we need a sorted copy of the bin's data
            std::vector<float> sorted_bin = bin;
            std::sort(sorted_bin.begin(), sorted_bin.end());

            float median;
            size_t n = sorted_bin.size();
            if (n % 2 == 0) {
                median = 0.5f * (sorted_bin[n/2 - 1] + sorted_bin[n/2]);
            } else {
                median = sorted_bin[n/2];
            }

            std::cout << relative_time_ms << "\t\t"
                      << std::fixed << std::setprecision(4) << median << "\t"
                      << bin.size() << std::endl;
        }
    }
    std::cout << "-----------------------\n" << std::endl;
}


// --- Main Program Logic ---

int main(int argc, char **argv) {
    using namespace popl;

    if (geteuid() != 0) {
        std::cerr << "Warning: This program works better with root privileges to read from sysfs with low latency." <<
                std::endl;
        std::cerr << "Please run with sudo." << std::endl;
    }

    // --- Command Line Parsing ---
    OptionParser op("Allowed options");
    auto help_option = op.add<Switch>("h", "help", "produce help message");
    auto period_opt = op.add<Value<int> >("p", "period", "Period of the worker task in milliseconds", 150);
    auto duty_cycle_opt = op.add<Value<int> >("d", "duty-cycle", "Duty cycle of the worker task in percent (10-90)",
                                              50);
    auto cycles_opt = op.add<Value<int> >("c", "cycles", "How many busy/wait cycles to run per core", 122);
    auto rounds_opt = op.add<Value<int> >("r", "rounds", "How many times to cycle through all cores", 1);
    auto outfile_opt = op.add<Value<std::string> >("o", "output", "Output filename for results",
                                                   "results/output.csv");

    op.parse(argc, argv);

    if (help_option->is_set()) {
        std::cout << op << std::endl;
        return 0;
    }

    // --- Experiment Setup ---
    const int num_hardware_threads = std::thread::hardware_concurrency();
    const int measurement_core = 0; // Pinned core for readout
    std::cout << "System has " << num_hardware_threads << " hardware threads." << std::endl;
    std::cout << "Measurement thread will be pinned to core " << measurement_core << "." << std::endl;

    PmTableReader pm_table_reader;
    const size_t n_measurements = pm_table_reader.getPmTableSize() / sizeof(float);

    // --- Pre-allocation of Memory ---
    const size_t max_samples_per_run = (period_opt->value() / 1) * cycles_opt->value() + 1000;
    // 1ms sample rate + buffer
    const size_t max_transitions_per_run = cycles_opt->value() * 2;

    std::vector<MeasurementSample> measurement_storage;
    measurement_storage.reserve(max_samples_per_run);
    for (size_t i = 0; i < max_samples_per_run; ++i) {
        measurement_storage.emplace_back(n_measurements);
    }
    assert(measurement_storage.size() == max_samples_per_run);
    assert(measurement_storage[0].measurements.size() == n_measurements);
    std::vector<WorkerTransition> transition_storage(max_transitions_per_run);
    size_t actual_sample_count = 0;
    size_t actual_transition_count = 0;

    // NEW: Instantiate the storage for our eye diagram
    EyeDiagramStorage v17_eye_storage;

    // File for final output
    std::ofstream outfile(outfile_opt->value());
    outfile << "round,core_id,timestamp_ns,worker_state"; //"sensor1,sensor2\n";
    for (int i=0; i< n_measurements; i++) {
        outfile << ",v" << std::to_string(i);
    }
    outfile << std::endl;

    // --- Main Experiment Loop ---
    for (int round = 0; round < rounds_opt->value(); ++round) {
        std::cout << "========== STARTING ROUND " << round + 1 << " OF " << rounds_opt->value() << " ==========" <<
                std::endl;

        for (int core_to_test = 1; core_to_test < num_hardware_threads; ++core_to_test) {
            if (core_to_test == measurement_core) continue;

            std::cout << "Starting test on core " << core_to_test << std::endl;

            // Reset state for the new run
            g_run_measurement = false;
            g_run_worker = false;
            // g_worker_state = 0;
            actual_sample_count = 0;
            actual_transition_count = 0;
            v17_eye_storage.clear(); // NEW: Clear eye diagram data for the new core run

            // --- Launch Threads for the current core test ---
            std::thread measurement_thread(measurement_thread_func, measurement_core,
                                           std::ref(measurement_storage), std::ref(actual_sample_count),
                                           std::ref(pm_table_reader),
                                           std::ref(v17_eye_storage)); // NEW: Pass storage to thread

            std::thread worker_thread(worker_thread_func, core_to_test,
                                      period_opt->value(), duty_cycle_opt->value(), cycles_opt->value(),
                                      std::ref(transition_storage), std::ref(actual_transition_count));

            // Give threads a moment to initialize and set affinity
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // Signal threads to start the measurement and workload
            g_run_measurement.store(true, std::memory_order_release);
            g_run_worker.store(true, std::memory_order_release);

            // Wait for the worker to finish its cycles
            worker_thread.join();

            // Signal the measurement thread to stop and wait for it to finish
            g_run_measurement.store(false, std::memory_order_release);
            measurement_thread.join();

            std::cout << "Finished test on core " << core_to_test << std::endl;

            // --- Analyze and Store Results (between core runs) ---
            analyze_and_print_results(core_to_test, measurement_storage, actual_sample_count,
                                      transition_storage, actual_transition_count,
                                      v17_eye_storage); // NEW: Pass storage to analysis function

            // Write the raw data for this run to the file
            for (size_t i = 0; i < actual_sample_count; ++i) {
                auto const &s = measurement_storage[i];
                outfile << round << ","
                        << core_to_test << ","
                        << std::chrono::duration_cast<std::chrono::nanoseconds>(s.timestamp.time_since_epoch()).
                        count() << ","
                        << s.worker_state;
                // << s.mock_sensor_1 << ","
                // << s.mock_sensor_2 << "\n";
                for (const auto &e: s.measurements) {
                    outfile << "," << e;
                }
                outfile << std::endl;
            }
        }
    }

    std::cout << "========== EXPERIMENT COMPLETE ==========" << std::endl;
    outfile.close();
    std::cout << "Results saved to " << outfile_opt->value() << std::endl;

    return 0;
}
