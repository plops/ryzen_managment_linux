/** @file measure.cpp
 *  @brief Measurement harness: spawns worker and measurement threads, records samples and analyzes eye diagrams.
 *
 *  This file coordinates the experiment loop, pre-allocates buffers and writes results.
 */

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

#include "measurement_types.hpp"
#include "pm_table_reader.hpp"
#include "eye_diagram.hpp"
#include "eye_capturer.hpp"
#include "folly/stats/StreamingStats.h"

// --- Global Atomic Flags for Thread Synchronization ---
// These are used to signal start/stop without using mutexes
/**
 * @brief Global flag to control measurement thread execution.
 *
 * When true the measurement thread samples sensors at 1ms intervals.
 */
std::atomic<bool> g_run_measurement = false;

/**
 * @brief Global flag to control worker thread execution.
 *
 * When true the worker thread begins its busy/wait cycles.
 */
std::atomic<bool> g_run_worker = false;

/**
 * @brief Global worker state communicated to the measurement thread.
 *
 * Value is 0 when the worker is idle (waiting) and 1 when the worker is busy.
 */
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

/**
 * @brief Measurement thread function.
 *
 * Samples the pm_table at a fixed 1ms rate, stores a MeasurementSample in the provided buffer,
 * and forwards samples to the EyeCapturer for binning.
 *
 * @param core_id CPU core to pin the measurement thread to.
 * @param storage Pre-allocated vector of MeasurementSample to fill.
 * @param sample_count Out parameter updated with the number of samples written.
 * @param pm_table_reader Reader object for the pm_table sysfs blob.
 * @param capturer EyeCapturer that bins samples into the EyeDiagramStorage.
 */
void measurement_thread_func(int core_id,
                             std::vector<MeasurementSample> &storage,
                             size_t &sample_count,
                             PmTableReader &pm_table_reader,
                             EyeCapturer &capturer) {
    if (!set_thread_affinity(core_id)) {
        std::cerr << "Warning: Failed to set measurement thread affinity to core " << core_id << std::endl;
    }

    // Wait for the signal to start
    while (!g_run_measurement.load(std::memory_order_acquire));

    const auto sample_period = std::chrono::milliseconds(1);
    auto next_sample_time = Clock::now();
    sample_count = 0;

    while (g_run_measurement.load(std::memory_order_acquire)) {
        // Record timestamp and state immediately
        TimePoint timestamp = Clock::now();
        int worker_state = g_worker_state.load(std::memory_order_relaxed);

        // Store in pre-allocated buffer
        if (sample_count < storage.size()) {
            auto &target = storage[sample_count];
            auto floatPtr = target.measurements.data();
            auto charPtr = reinterpret_cast<char *>(floatPtr);
            // Read sensors
            pm_table_reader.read(charPtr);
            target.timestamp = timestamp;
            target.worker_state = worker_state;
            // process into eye capturer (bins per sensor)
            capturer.process_sample(timestamp, worker_state, target.measurements);
            sample_count++;
        }
        assert(sample_count < storage.size());

        next_sample_time += sample_period;
        if (Clock::now() > next_sample_time) {
            std::cerr << "Can't maintain sample rate" << std::endl;
        }
        std::this_thread::sleep_until(next_sample_time);
    }
}

/**
 * @brief Worker thread function implementing the busy/wait workload.
 *
 * The worker toggles g_worker_state between busy (1) and wait (0) according to the provided
 * period and duty-cycle. Each transition is optionally stored in the provided storage vector.
 *
 * @param core_id CPU core to pin the worker thread to.
 * @param period_ms Length of one duty cycle in milliseconds.
 * @param duty_cycle_percent Percentage of the period spent busy (1..99).
 * @param num_cycles Number of busy/wait cycles to execute.
 * @param transition_count Out parameter updated with the number of transitions recorded.
 */
void worker_thread_func(int core_id,
                        int period_ms,
                        int duty_cycle_percent,
                        int num_cycles,
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

        auto busy_start = Clock::now();
        while ((Clock::now() - busy_start) < busy_duration) {
            integer_alu_workload(1000); // Inner loop to keep CPU busy
        }

        // --- WAITING PHASE ---
        g_worker_state.store(0, std::memory_order_relaxed);

        std::this_thread::sleep_for(wait_duration);
    }
}

/**
 * @brief Calculate a trimmed mean (robust average).
 *
 * Sorts a copy of the input data and removes trim_percentage% of samples
 * from each side before averaging the remainder. Falls back to median if
 * too few samples remain after trimming.
 *
 * @param data Input sample vector.
 * @param trim_percentage Percentage of samples to remove from each tail (0..50).
 * @return Trimmed mean or median if trimming removed too many elements.
 */
static float calculate_trimmed_mean(const std::vector<float> &data, float trim_percentage) {
    if (data.empty()) return 0.0f;
    std::vector<float> sorted = data;
    std::sort(sorted.begin(), sorted.end());
    size_t n = sorted.size();
    size_t trim_count = static_cast<size_t>((trim_percentage / 100.0f) * n);
    if (2 * trim_count >= n) {
        // Not enough elements after trimming; return median as fallback
        return sorted[n / 2];
    }
    auto first = sorted.begin() + trim_count;
    auto last = sorted.end() - trim_count;
    double sum = std::accumulate(first, last, 0.0);
    size_t count = std::distance(first, last);
    return static_cast<float>(sum / (count ? count : 1));
}

/**
 * @brief Analyze collected measurements and print results.
 *
 * Computes simple busy/wait means (for sensor 17) and prints eye-diagram statistics
 * (median and trimmed mean) for every sensor stored in EyeDiagramStorage.
 *
 * @param core_id Core under test (for labeling).
 * @param measurements Vector of MeasurementSample containing raw capture data.
 * @param sample_count Number of samples actually recorded.
 * @param transitions Vector of WorkerTransition events recorded by the worker thread.
 * @param transition_count Number of transitions recorded.
 * @param eye_storage Pre-populated EyeDiagramStorage used to compute eye-diagram stats.
 */
void analyze_and_print_results(int core_id,
                               const std::vector<MeasurementSample> &measurements, size_t sample_count,
                               size_t transition_count,
                               const EyeDiagramStorage &eye_storage) {
    std::cout << "--- Analyzing Core " << core_id << " ---" << std::endl;
    std::cout << "Collected " << sample_count << " measurement samples." << std::endl;
    std::cout << "Recorded " << transition_count << " worker transitions." << std::endl;

    // compute simple busy/wait means for sensor 17 as before (unchanged)
    std::vector<double> busy_samples;
    std::vector<double> wait_samples;
    for (size_t i = 0; i < sample_count; ++i) {
        if (measurements[i].worker_state == 1) busy_samples.push_back(measurements[i].measurements[17]);
        else wait_samples.push_back(measurements[i].measurements[17]);
    }
    if (busy_samples.empty() || wait_samples.empty()) {
        std::cout << "Not enough data to compute statistics." << std::endl;
    } else {
        double busy_mean = std::accumulate(busy_samples.begin(), busy_samples.end(), 0.0) / busy_samples.size();
        double wait_mean = std::accumulate(wait_samples.begin(), wait_samples.end(), 0.0) / wait_samples.size();
        std::cout << "Mean THM value while BUSY:   " << busy_mean << std::endl;
        std::cout << "Mean THM value while WAITING: " << wait_mean << std::endl;
        std::cout << "Mean Correlation (Difference): " << busy_mean - wait_mean << std::endl;
    }

    // --- Eye Diagram output for every sensor ---
    const float trim_percent = 10.0f;
    size_t n_sensors = eye_storage.bins.empty() ? 0 : eye_storage.bins[0].size();

    for (size_t sensor = 0; sensor < n_sensors; ++sensor) {
        std::cout << "\n--- Sensor v" << sensor << " Eye Diagram (Median) ---" << std::endl;
        std::cout << "Captured " << eye_storage.event_count << " rising edge events." << std::endl;
        std::cout << "Time(ms)\tMedian\tSamples" << std::endl;
        for (int i = 0; i < EyeDiagramStorage::NUM_BINS; ++i) {
            const auto &bin = eye_storage.bins[i][sensor];
            if (!bin.empty()) {
                int relative_time_ms = i - EyeDiagramStorage::ZERO_OFFSET_BINS;
                std::vector<float> sorted_bin = bin;
                std::sort(sorted_bin.begin(), sorted_bin.end());
                float median;
                size_t n = sorted_bin.size();
                if (n % 2 == 0) median = 0.5f * (sorted_bin[n / 2 - 1] + sorted_bin[n / 2]);
                else median = sorted_bin[n / 2];
                std::cout << relative_time_ms << "\t\t" << std::fixed << std::setprecision(4) << median
                        << "\t" << bin.size() << std::endl;
            }
        }

        std::cout << "\n--- Sensor v" << sensor << " Eye Diagram (TrimmedMean " << trim_percent << "%) ---" <<
                std::endl;
        std::cout << "Time(ms)\tTrimmedMean\tSamples" << std::endl;
        for (int i = 0; i < EyeDiagramStorage::NUM_BINS; ++i) {
            const auto &bin = eye_storage.bins[i][sensor];
            if (!bin.empty()) {
                int relative_time_ms = i - EyeDiagramStorage::ZERO_OFFSET_BINS;
                float robust_mean = calculate_trimmed_mean(bin, trim_percent);
                std::cout << relative_time_ms << "\t\t" << std::fixed << std::setprecision(4) << robust_mean
                        << "\t" << bin.size() << std::endl;
            }
        }
    }
    std::cout << "-----------------------\n" << std::endl;
}


// --- Main Program Logic ---

/**
 * @brief Program entrypoint: parses CLI, allocates buffers and runs experiments.
 *
 * The main function pre-allocates measurement buffers, constructs the pm_table reader,
 * eye storage and capturer, and iterates through rounds/cores executing the workload.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status (0 on success).
 */
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
    auto cycles_opt = op.add<Value<int> >("c", "cycles", "How many busy/wait cycles to run per core", 13);
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

    {
        // Read a few times to determine the pm_table entries that are changing
        std::vector<float> measurements(n_measurements);
        std::vector<folly::StreamingStats<float, float> > stats(n_measurements);
        std::vector<bool> interesting(n_measurements, false);
        const auto sample_period = std::chrono::milliseconds(1);
        auto next_sample_time = Clock::now();
        for (int count = 0; count < 997; count++) {
            // Read sensors
            pm_table_reader.read(reinterpret_cast<char *>(measurements.data()));
            for (int i = 0; i < n_measurements; ++i) {
                stats[i].add(measurements[i]);
            }
            next_sample_time += sample_period;
            std::this_thread::sleep_until(next_sample_time);
        }
        int count_interesting = 0;
        for (int i = 0; i < n_measurements; ++i) {
            if (stats[i].sampleVariance() > 0.f) {
                interesting[i] = true;
                count_interesting++;
            }
        }
        std::cout << "count_interesting = " << count_interesting << std::endl;
    }


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
    size_t actual_sample_count = 0;
    size_t actual_transition_count = 0;

    // NEW: Instantiate the storage for our eye diagram
    size_t expected_events = cycles_opt->value(); // use cycles_opt value in original code; kept simple here
    EyeDiagramStorage eye_storage(n_measurements, expected_events);
    EyeCapturer capturer(eye_storage, n_measurements);

    // File for final output
    std::ofstream outfile(outfile_opt->value());
    outfile << "round,core_id,timestamp_ns,worker_state"; //"sensor1,sensor2\n";
    for (int i = 0; i < n_measurements; i++) {
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
            eye_storage.clear(); // call per run to reset while keeping allocation

            // --- Launch Threads for the current core test ---
            std::thread measurement_thread(measurement_thread_func, measurement_core,
                                           std::ref(measurement_storage), std::ref(actual_sample_count),
                                           std::ref(pm_table_reader),
                                           std::ref(capturer));

            std::thread worker_thread(worker_thread_func, core_to_test,
                                      period_opt->value(), duty_cycle_opt->value(), cycles_opt->value(),
                                      std::ref(actual_transition_count));

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
                                      actual_transition_count,
                                      eye_storage); // NEW: Pass storage to analysis function

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
