# pm_measure Architecture

`pm_measure` is a tool designed to measure and analyze the impact of CPU core activity on power management (PM) sensor values on AMD Ryzen systems. It operates by creating a controlled workload on one core while a high-frequency measurement thread on another core samples sensor data from the `ryzen_smu` driver's `pm_table`.

The primary goal is to generate "eye diagrams" that show how sensor values change in response to the worker thread transitioning between busy and idle states. The application can run in a command-line mode for batch experiments or an interactive GUI mode for real-time visualization.

## Entry Point and Orchestration

The application entry point is in `main.cpp`. Its primary responsibilities are:
1.  Parsing command-line arguments using `popl`.
2.  Validating system configuration (e.g., checking for `ryzen_smu` driver).
3.  Based on arguments, launching either the command-line experiment loop or the interactive GUI.

*   **CLI Mode**: The main thread directly orchestrates the experiment, iterating through specified CPU cores, spawning threads, collecting data, and writing results to a CSV file.
*   **GUI Mode**: An instance of `GuiRunner` is created, which takes over the main thread to manage the GUI window and render loop. The `GuiRunner` then spawns a separate experiment thread to perform the measurement tasks in the background.

## Core Threads

The application's core logic is built around three specialized threads:

1.  **Experiment Thread** (or **Main Thread** in CLI mode):
    *   Manages the overall experiment flow, iterating through specified CPU cores and rounds.
    *   Pre-allocates a large memory buffer (`LockedBuffer`) for measurement data.
    *   Initializes data storage structures (`EyeDiagramStorage`).
    *   Spawns and manages the lifecycle of the worker and measurement threads for each test run.
    *   In GUI mode, it communicates status updates and swaps data buffers with the GUI thread.

2.  **Worker Thread** (`worker_thread_func`):
    *   Pinned to a specific CPU core under test.
    *   Executes a simple busy/wait cycle with a configurable period and duty cycle to create a predictable load.
    *   Communicates its state (0 for idle, 1 for busy) to the measurement thread via a global atomic flag.

3.  **Measurement Thread** (`measurement_thread_func`):
    *   Pinned to a dedicated, isolated CPU core to ensure consistent timing.
    *   Uses `RealtimeGuard` to acquire real-time scheduling priority (`SCHED_FIFO`).
    *   Runs a tight loop, sampling the `pm_table` at a fixed high-frequency interval (e.g., 1ms).
    *   Uses a `PmTableReader` to read sensor data from sysfs.
    *   Feeds each sample (timestamp, worker state, sensor values) into an `EyeCapturer` instance, which processes the data in real-time.

## Key Classes and Data Structures

Classes are grouped by their role in the application.

### System and OS Abstractions

*   `PmTableReader`: A simple helper class that opens `/sys/kernel/ryzen_smu_drv/pm_table` on construction and provides a method to read the entire table into a buffer. It also reads and caches the table size.
*   `LockedBuffer`: An RAII wrapper for allocating a large, page-aligned memory buffer. It attempts to lock the buffer into RAM using `mlock` to prevent page faults during the critical measurement loop, falling back to a standard `malloc` if permissions are insufficient.
*   `RealtimeGuard`: An RAII helper that elevates the current thread's scheduling policy to `SCHED_FIFO` and pins it to a specific CPU core. It automatically restores the original scheduling policy and affinity when it goes out of scope, ensuring clean teardown.

### Data Capture and Storage

*   `EyeDiagramStorage`: The primary data container for the eye diagram. It holds binned sensor measurements. It is configured with a time window (e.g., -50ms to +150ms around an event) and allocates a multi-dimensional vector (`bins`) to store all samples that fall into each time bin for each monitored sensor.
*   `EyeCapturer`: A state machine that processes the raw sample stream from the measurement thread. It watches the worker's state and, upon detecting a rising edge (idle to busy), it begins capturing subsequent samples. It calculates the time delta from the rising edge for each sample and places the sensor values into the correct time bin in the `EyeDiagramStorage`.

### GUI Components

*   `GuiRunner`: The main class for the interactive mode. It initializes the GLFW window and ImGui context. It manages the main render loop, drawing the user interface. It also launches the experiment thread and manages data exchange using a double-buffering scheme with two `EyeDiagramStorage` objects to prevent race conditions between the measurement and render threads.
*   `GuiDataCache`: A helper that decouples data processing from rendering. It holds render-ready plot data (`EyePlotData`). Periodically, it accesses the latest `EyeDiagramStorage` from the experiment, calculates statistics (like medians) for each time bin, and caches the results. The render loop then uses this pre-processed data to draw the plots, ensuring a smooth frame rate.
*   `EyePlotData`: A simple struct containing the data needed to render a single plot: the x-axis values (time) and y-axis values (e.g., median sensor reading).

### Core Data Types

*   `measurement_types.hpp`: Defines fundamental types like `TimePoint` from `std::chrono::steady_clock`, ensuring consistent, high-precision timing throughout the application.
