# pm_measure Architecture (v2 - Decoupled Pipeline)

`pm_measure` is a tool designed to measure and analyze the impact of CPU core activity on power management (PM) sensor values on AMD Ryzen systems. It operates by creating a controlled workload on one core while a high-frequency measurement thread on another core samples sensor data from the `ryzen_smu` driver's `pm_table`.

The primary goal is to generate "eye diagrams" that show how sensor values change in response to the worker thread transitioning between busy and idle states. The application runs in an interactive GUI mode for real-time visualization and control.

## High-Level Design: A Three-Tier Streaming Pipeline

The new architecture is a **three-tier, multi-threaded pipeline** designed for low latency, high throughput, and clear separation of concerns. This design ensures that high-priority data acquisition is never blocked by data processing or UI rendering, and the GUI remains responsive at 60Hz.

The three main tiers are:
1.  **Measurement:** A real-time thread dedicated solely to acquiring sensor data.
2.  **Processing:** A background thread that consumes raw data, performs all calculations (triggering, binning, statistics), and prepares data for visualization.
3.  **Visualization:** The main GUI thread, which acts as a "dumb" renderer of the pre-processed data.

Communication between threads is handled by high-performance, lock-free (or low-contention) data structures.

```
+------------------+     SPSC Queue     +-------------------+   Double Buffer    +------------------+
| Measurement      | -----------------> | Processing        | ----------------> | GUI/Visualization|
| (Real-time Core) | (RawSample Stream) | (Background Core) | (DisplayData Ptr) | (Main Thread)    |
+------------------+                    +-------------------+ <---------------- +------------------+
                                                 ^             Command Queue
                                                 |            (User Actions)
                                                 +------------------------------+
```

## Entry Point and Orchestration

The application entry point remains in `measure.cpp`, which handles command-line parsing (`popl`), initial sensor discovery, and launching the central orchestrator, the `GuiRunner` class.

The `GuiRunner` takes over the main thread and is responsible for:
1.  Initializing and managing the main GUI window (GLFW, ImGui, ImPlot).
2.  Setting up the inter-thread communication channels (`folly::ProducerConsumerQueue`, `CommandQueue`, and the double-buffered `DisplayData` pointers).
3.  Spawning and managing the lifecycle of the three background threads: **Measurement**, **Processing**, and **Worker**.
4.  Running the main GUI render loop.

## Core Threads

The application's logic is now cleanly partitioned across four distinct threads.

1.  **GUI/Visualization Thread (Main Thread)**:
    *   Managed by `GuiRunner`.
    *   Runs the 60Hz render loop.
    *   Renders the UI, including plots and control widgets (e.g., core selection slider).
    *   For each frame, it performs a lock-free read of an **atomic pointer** to get the latest `DisplayData` prepared by the Processing thread. It performs **no calculations**; it only draws the data it is given.
    *   When the user interacts with a control (e.g., changes the test core), it pushes a command object onto a thread-safe `CommandQueue` to be handled by the Processing thread.

2.  **Measurement Thread** (`measurement_thread_func`):
    *   Pinned to a dedicated, isolated CPU core (e.g., core 0) with `SCHED_FIFO` real-time priority, managed by `RealtimeGuard`.
    *   Its sole responsibility is to sample the `pm_table` at a precise 1kHz interval.
    *   The timing loop uses a hybrid `clock_nanosleep` and spin-wait for accuracy.
    *   On each tick, it reads the sensor data into a `RawSample` struct and pushes it into a **`folly::ProducerConsumerQueue`**, a high-performance, single-producer, single-consumer lock-free queue.
    *   This thread is kept extremely lean to guarantee its timing and prevent data loss.

3.  **Processing Thread** (`GuiRunner::run_processing_thread`):
    *   The new computational core of the application.
    *   Runs in a continuous loop on a background core.
    *   **Consumes** `RawSample` data from the SPSC queue.
    *   Maintains a short history of recent samples (`std::deque<RawSample>`) to provide data for the pre-trigger part of the eye diagram (`window_before_ms`).
    *   Implements the state machine logic (previously in `EyeCapturer`) to detect rising edges (idle-to-busy transitions) of the worker state.
    *   When a rising edge is detected, it combines the sample history and newly captured samples to form a complete trace.
    *   It **bins** the samples from this trace into an internal **accumulation buffer** (`std::vector<std::vector<std::deque<float>>>`), which stores the values for many traces.
    *   It calculates statistics across all accumulated traces: the **trimmed mean**, **min/max envelopes**.
    *   It populates a "back" `DisplayData` buffer with these final, render-ready plot points.
    *   Finally, it **atomically swaps a pointer**, making the newly completed `DisplayData` available to the GUI thread for its next frame. This is the core of the lock-free data handoff.
    *   It also polls the `CommandQueue` to react to user input from the GUI, such as clearing buffers when the test core changes.

4.  **Worker Thread** (`GuiRunner::run_worker_thread` -> `worker_thread_func`):
    *   Pinned to the CPU core under test.
    *   Executes a simple busy/wait cycle with a configurable period and duty cycle.
    *   Communicates its current state (0 for idle, 1 for busy) to the Measurement thread via the global atomic `g_worker_state`.

## Key Classes and Data Structures

The class structure has been simplified to reflect the new pipeline.

### System and OS Abstractions

*   `PmTableReader`: Unchanged. A helper class to read the `/sys/kernel/ryzen_smu_drv/pm_table` blob.
*   `RealtimeGuard`: Unchanged. An RAII helper to manage real-time scheduling (`SCHED_FIFO`) and CPU affinity for a thread.
*   `LockedBuffer`: No longer a central component of the streaming architecture, but may be used for other purposes. Data is now primarily streamed through queues.

### Data Flow and Communication Primitives

*   **`folly::ProducerConsumerQueue<RawSample>`**: The high-performance, wait-free queue that decouples the Measurement thread from the Processing thread. This is critical for ensuring the measurement loop is never stalled.
*   **Double-Buffer of `DisplayData`**: The `GuiRunner` owns two complete sets of `DisplayData` objects (one for each interesting sensor). The Processing thread writes to the inactive set. An `std::vector<std::atomic<DisplayData*>>` provides the GUI thread with safe, lock-free, read-only access to the active set.
*   `CommandQueue`: A simple thread-safe queue (`std::queue` + `std::mutex`) used to send commands from the GUI to the Processing thread.

### Core Data Types

*   `RawSample`: A struct holding a single timestamp, the worker state, and an array of all sensor values from one read of the PM table. This is the data unit passed through the SPSC queue.
*   `DisplayData`: A struct containing only the data needed for rendering one plot: vectors for x-values (time) and y-values (trimmed mean, min, max), plus metadata like the window size. It contains no raw samples.

### GUI Components

*   `GuiRunner`: The central orchestrator class. Owns the threads, communication channels, and the main window.
*   `render_gui()`: A free function that takes the current `DisplayData` pointers and other GUI state and is responsible only for issuing ImGui/ImPlot draw calls.
