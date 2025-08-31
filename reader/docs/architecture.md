# pm_measure Architecture

`pm_measure` is a tool designed to measure and analyze the impact of CPU core activity on power management (PM) sensor values on AMD Ryzen systems. It operates by creating a controlled workload on one core while a high-frequency measurement thread on another core samples sensor data from the `ryzen_smu` driver's `pm_table`.

The primary goal is to generate "eye diagrams" that show how sensor values change in response to the worker thread transitioning between busy and idle states. The application runs in an interactive GUI mode for real-time visualization and control.

## Entry Point and Orchestration

The application entry point is in `measure.cpp`. Its primary responsibilities are:
1.  Parsing command-line arguments using `popl` for experiment parameters (e.g., period, duty cycle).
2.  Validating system configuration (e.g., checking for `ryzen_smu` driver and identifying which sensor values change).
3.  Pre-allocating a large, memory-locked buffer for raw sensor readings.
4.  Instantiating and launching the `GuiRunner`, which takes over the main thread to manage the application.

The application is now GUI-only. The `GuiRunner` class is the central orchestrator. It performs two main functions on two different threads:

1.  **GUI Thread (Main Thread)**: Manages the GUI window, handles user input, and runs the render loop using GLFW and ImGui.
2.  **Experiment Thread**: A background thread spawned by `GuiRunner`. It runs the measurement experiments, either automatically scanning through cores or running on a single core specified by the user in manual mode.

This separation ensures that the GUI remains responsive even while high-priority, real-time measurement tasks are running.

## Core Threads

The application's core logic is built around three specialized threads:

1.  **GUI Thread**:
    *   The application's main thread, managed by `GuiRunner`.
    *   Renders the user interface, including plots and control widgets (e.g., manual mode checkbox, core selection slider).
    *   Periodically, it reads processed data from a `GuiDataCache` to update the plots.
    *   Communicates with the experiment thread via atomic flags to switch between automatic and manual modes.

2.  **Experiment Thread** (`GuiRunner::run_experiment_thread`):
    *   Manages the overall experiment flow.
    *   In **Automatic Mode**, it iterates through specified CPU cores and rounds, clearing data for each new run.
    *   In **Manual Mode**, it continuously runs the experiment on a single CPU core selected by the user via the GUI.
    *   For each test run, it spawns and manages the lifecycle of the worker and measurement threads.
    *   It manages a double-buffer of `EyeDiagramStorage` objects to communicate data to the GUI thread without locks. After a run is complete, it atomically swaps a pointer to make the newly filled buffer available for reading by the GUI.

3.  **Worker Thread** (`worker_thread_func`):
    *   Pinned to a specific CPU core under test.
    *   Executes a simple busy/wait cycle with a configurable period and duty cycle to create a predictable load.
    *   Communicates its state (0 for idle, 1 for busy) to the measurement thread via a global atomic flag (`g_worker_state`).

4.  **Measurement Thread** (`measurement_thread_func`):
    *   Pinned to a dedicated, isolated CPU core (core 0) to ensure consistent timing.
    *   Uses `RealtimeGuard` to acquire real-time scheduling priority (`SCHED_FIFO`).
    *   Runs a tight loop, sampling the `pm_table` at a fixed high-frequency interval (e.g., 1ms). Timing is maintained by a hybrid `clock_nanosleep` and spin-wait loop.
    *   Uses a `PmTableReader` to read sensor data from sysfs directly into a pre-allocated, memory-locked buffer.
    *   Feeds each sample (timestamp, worker state, sensor values) into an `EyeCapturer` instance, which processes the data in real-time.

## Key Classes and Data Structures

Classes are grouped by their role in the application.

### System and OS Abstractions

*   `PmTableReader`: A simple helper class that opens `/sys/kernel/ryzen_smu_drv/pm_table` on construction and provides a method to read the entire table into a buffer. It also reads and caches the table size.
*   `LockedBuffer`: An RAII wrapper for allocating a large, page-aligned memory buffer. It attempts to lock the buffer into RAM using `mlock` to prevent page faults during the critical measurement loop, falling back to a standard `malloc` if permissions are insufficient.
*   `RealtimeGuard`: An RAII helper that elevates the current thread's scheduling policy to `SCHED_FIFO` and pins it to a specific CPU core. It automatically restores the original scheduling policy and affinity when it goes out of scope, ensuring clean teardown.

### Data Capture and Storage

*   `EyeDiagramStorage`: The primary data container for the eye diagram. It holds binned sensor measurements. It is configured with a time window (e.g., -50ms to +150ms around an event) and allocates a multi-dimensional vector (`bins`) to store all samples that fall into each time bin for each monitored sensor. The `GuiRunner` maintains two instances of this class for double-buffering.
*   `EyeCapturer`: A state machine that processes the raw sample stream from the measurement thread. It watches the worker's state and, upon detecting a rising edge (idle to busy), it begins capturing subsequent samples. It calculates the time delta from the rising edge for each sample and places the sensor values into the correct time bin in the `EyeDiagramStorage`.

### GUI Components

*   `GuiRunner`: The main class for the application. It initializes the GLFW window and ImGui context. It spawns the experiment thread and manages the main render loop. It facilitates communication between the GUI and experiment threads using atomic variables for control (e.g., `manual_mode_`) and a double-buffering scheme for data.
*   `GuiDataCache`: A helper that decouples data processing from rendering. It holds render-ready plot data (`EyePlotData`). Periodically, the GUI thread calls `update()` on this cache. Inside `update()`, it accesses the latest `EyeDiagramStorage` made available by the experiment thread. For each sensor and for each time bin within that sensor's data, it performs the following steps:
    1.  It takes all the raw float samples collected in that specific time bin.
    2.  It sorts the samples.
    3.  It calculates the **median** value of the sorted samples.
    4.  This single median value becomes the Y-value for the plot at that point in time (the X-value).
    This process reduces the potentially thousands of individual data points in each bin to a single, statistically robust point, which is then used to draw the line plots. This ensures a smooth frame rate and a clear visualization of the central tendency of the sensor data over time.
*   `EyePlotData`: A simple struct containing the data needed to render a single plot: the x-axis values (time) and y-axis values (e.g., median sensor reading).

### Core Data Types

*   `measurement_types.hpp`: Defines fundamental types like `TimePoint` from `std::chrono::steady_clock`, ensuring consistent, high-precision timing throughout the application.
