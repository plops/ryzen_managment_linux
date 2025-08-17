# Ryzen PM Table Monitor

A high-performance Python application for real-time visualization of AMD Ryzen processor metrics from the `pm_table`
interface, provided by the `ryzen_smu` kernel driver.

This tool reads the `pm_table` data at a high frequency (targeting 1kHz), allowing users to plot and analyze various
sensor readings like temperature, power, voltage, and clock speeds in a highly responsive graph. The selection of
metrics is fully configurable through a simple TOML file.

## Key Features

- **Real-Time Plotting**: Visualize processor metrics with low latency.
- **High-Frequency Data Acquisition**: A dedicated thread reads the `pm_table` file at a target rate of 1000 Hz.
- **Fully Configurable**: Use the `config.toml` file to specify which metrics you want to plot. Define the metric's
  name, its offset in the `pm_table`, and its plot color.
- **Interactive Graphs**: Zoom (right-click + drag) and pan (left-click + drag) the time axis to inspect data.
- **Efficient Data Display**: Utilizes `pyqtgraph` for high-performance plotting. When zoomed out, the graph
  automatically downsamples the data, showing the minimum and maximum values for each pixel range, which effectively
  visualizes the data envelope.
- **Modern Tooling**: Built with Python, PyQt6, and the high-performance `pyqtgraph` library. Uses `uv` for streamlined
  dependency and virtual environment management.

## Prerequisites

1. **`ryzen_smu` Kernel Module**: You must have the [ryzen_smu](https://github.com/amkillam/ryzen_smu) kernel module
   installed and loaded. This is required for the application to access the `/sys/kernel/ryzen_smu_drv/pm_table` file.
2. **Python**: Python 3.10 or newer is recommended.
3. **`uv`**: This project uses `uv` for package management. You can install it via `pip`:
   ```bash
   pip install uv
   ```

## Installation and Setup

1. **Clone the repository**:
   ```bash
   git clone https://github.com/plops/ryzen_managment_linux
   cd ryzen_managment_linux/pyrymon
   ```

2. **Create the virtual environment, install dependencies and run**:
   `uv` will create a `.venv` directory in your project folder.
   ```bash
   uv run python main.py
   ```
   You may need to give read permissions to the pm_table for your user: `sudo chmod a+r /sys/kernel/ryzen_smu_drv/pm_table`

## Configuration

The metrics to be plotted are defined in the `config.toml` file. You must configure this file with the correct offsets
for your specific CPU and BIOS (AGESA) version.

The structure of the `pm_table` is not officially documented by AMD and can vary. You may need to refer to projects like
`ryzen_monitor` or experiment to find the correct float offsets for the data you want to visualize.

**Example `config.toml`:**

```toml
# ryzen_pm_monitor/config.toml

[metrics]
# The 'offset' is the zero-based index of the 32-bit float value
# in the binary pm_table data.
# The 'color' is a single character code used by pyqtgraph (r, g, b, c, m, y, k, w).

"CPU_Temp" = { offset = 10, color = "r" }
"CPU_Power" = { offset = 12, color = "g" }
"GFX_Temp" = { offset = 25, color = "b" }
"Core0_Voltage" = { offset = 50, color = "y" }
"Core1_Voltage" = { offset = 51, color = "c" }
"Core2_Voltage" = { offset = 52, color = "m" }
```

## Usage

After setting up the environment and configuring your metrics, run the application from the project's root directory:

- The main window will appear.
- Use the checkboxes at the bottom to enable or disable plotting for each metric defined in your `config.toml`.
- **Zoom**: Right-click and drag horizontally on the plot.
- **Pan**: Left-click and drag on the plot.
- **Reset View**: Double-click on the plot. (Note: This is not working, yet)

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.

## Acknowledgments

This tool would not be possible without the foundational work done by the developers of the `ryzen_smu` kernel driver
and the `ryzen_monitor` utility, who have reverse-engineered the interfaces to AMD's System Management Unit.