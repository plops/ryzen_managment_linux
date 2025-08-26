# plot_data.py

import struct
import pandas as pd
import matplotlib
import time
import datetime

matplotlib.use('Qt5Agg')
import matplotlib.pyplot as plt

plt.ion()
import numpy as np
from pathlib import Path

# --- Configuration ---
LOG_FILE_PATH = "pm_table_log.bin"

# --- PM Table Metric Offsets (in bytes) ---
# NOTE: These offsets are for a common AMD Ryzen 5000 "Vermeer" PM Table (version 0x380905).
# You may need to adjust these for other CPUs (e.g., Matisse, Cezanne, Raphael).
METRIC_OFFSETS = {
    # Power Metrics (Watts)
    "socket_power": 0x48,
    "cpu_power": 0x4C,  # VDDCR_CPU_POWER
    "soc_power": 0x5C,  # VDDCR_SOC_POWER

    # Temperature Metrics (Celsius)
    "cpu_temp": 0x14,  # Tdie temperature

    # Core Metrics (Arrays for 16 cores)
    "core_power": 0x194,  # Array of 16 floats
    "core_freq_eff": 0x2CC,  # Array of 16 floats (Effective Frequency)
}
# Number of CPU cores the PM table provides data for (Vermeer provides 16 slots)
TABLE_CORE_COUNT = 16


def parse_log_file(filepath: Path) -> pd.DataFrame:
    """
    Parses the binary log file and extracts PM table metrics into a pandas DataFrame.

    The log file format for each record is:
    - 8 bytes: Timestamp (uint64, nanoseconds since epoch)
    - 8 bytes: Data Size (uint64, size of the pm_table data)
    - N bytes: Raw pm_table data (array of 32-bit floats)
    """
    if not filepath.exists():
        raise FileNotFoundError(f"Log file not found at: {filepath}")

    records = []

    with open(filepath, "rb") as f:
        while True:
            # Read the header for the next record
            header_data = f.read(16)
            if not header_data:
                break  # End of file

            # Unpack timestamp and data size from the header
            timestamp_ns, data_size = struct.unpack('<QQ', header_data)

            # Read the raw pm_table data
            pm_data = f.read(data_size)
            if len(pm_data) != data_size:
                print(f"Warning: Incomplete record found at end of file. Skipping.")
                break

            # --- Extract individual metrics from the raw data ---
            record = {
                "timestamp": pd.to_datetime(timestamp_ns, unit='ns')
            }

            try:
                # Unpack single float values
                for name, offset in METRIC_OFFSETS.items():
                    if 'core_' not in name:  # Handle single values
                        if offset + 4 <= data_size:
                            value = struct.unpack_from('<f', pm_data, offset)[0]
                            record[name] = value
                        else:
                            record[name] = np.nan

                # Unpack core-specific arrays
                offset = METRIC_OFFSETS["core_power"]
                num_bytes = TABLE_CORE_COUNT * 4
                if offset + num_bytes <= data_size:
                    core_powers = struct.unpack_from(f'<{TABLE_CORE_COUNT}f', pm_data, offset)
                    # Sum of valid core powers (ignore sleeping cores which report 0)
                    record['total_core_power'] = sum(p for p in core_powers if p > 0)

                offset = METRIC_OFFSETS["core_freq_eff"]
                if offset + num_bytes <= data_size:
                    core_freqs = struct.unpack_from(f'<{TABLE_CORE_COUNT}f', pm_data, offset)
                    # Get average and peak frequency of active cores
                    active_freqs = [f for f in core_freqs if f > 100]  # Filter out sleeping cores
                    if active_freqs:
                        record['avg_core_freq'] = np.mean(active_freqs)
                        record['peak_core_freq'] = np.max(active_freqs)
                    else:
                        record['avg_core_freq'] = 0
                        record['peak_core_freq'] = 0

                records.append(record)

            except struct.error as e:
                print(f"Warning: Could not unpack data for a record. Error: {e}. Skipping.")
                continue

    if not records:
        return pd.DataFrame()

    df = pd.DataFrame(records)
    df = df.set_index('timestamp')
    return df


def plot_data(df: pd.DataFrame):
    """Generates and displays plots from the parsed data."""
    if df.empty:
        print("DataFrame is empty. Nothing to plot.")
        return

    # Create subplots that share the x-axis (time)
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(15, 12), sharex=True)
    fig.suptitle('Ryzen SMU Power Metrics Analysis', fontsize=16)

    # --- Plot 1: Power Consumption ---
    ax1.set_title('Power Consumption')
    if 'socket_power' in df.columns:
        ax1.plot(df.index, df['socket_power'], label='Socket Power (PPT)', color='red')
    if 'total_core_power' in df.columns:
        ax1.plot(df.index, df['total_core_power'], label='Total Core Power', color='orange', linestyle='--')
    if 'soc_power' in df.columns:
        ax1.plot(df.index, df['soc_power'], label='SoC Power', color='purple', linestyle=':')
    ax1.set_ylabel('Power (Watts)')
    ax1.legend()
    ax1.grid(True, linestyle='--', alpha=0.6)

    # --- Plot 2: CPU Temperature ---
    ax2.set_title('CPU Temperature')
    if 'cpu_temp' in df.columns:
        ax2.plot(df.index, df['cpu_temp'], label='CPU Tdie', color='green')
    ax2.set_ylabel('Temperature (°C)')
    ax2.legend()
    ax2.grid(True, linestyle='--', alpha=0.6)

    # --- Plot 3: Core Frequency ---
    ax3.set_title('Core Frequency')
    if 'peak_core_freq' in df.columns:
        ax3.plot(df.index, df['peak_core_freq'], label='Peak Core Frequency', color='blue')
    if 'avg_core_freq' in df.columns:
        ax3.plot(df.index, df['avg_core_freq'], label='Average Active Core Freq', color='cyan', linestyle='--')
    ax3.set_ylabel('Frequency (MHz)')
    ax3.legend()
    ax3.grid(True, linestyle='--', alpha=0.6)

    # Improve formatting for the x-axis date labels
    fig.autofmt_xdate()
    plt.xlabel('Time')
    plt.tight_layout(rect=[0, 0.03, 1, 0.98])  # Adjust for suptitle
    plt.show()
    plt.savefig("o.png")

    # def main():
    #     """Main function to run the script."""
    #     try:
    #         log_path = Path(LOG_FILE_PATH)
    #         print(f"Attempting to parse log file: {log_path.resolve()}")
    #         data_df = parse_log_file(log_path)
    #         print(f"Successfully parsed {len(data_df)} records.")
    #
    #         if not data_df.empty:
    #             print("\nDisplaying first 5 rows of data:")
    #             print(data_df.head())
    #             plot_data(data_df)
    #         else:
    #             print("No data was parsed from the log file.")
    #
    #     except FileNotFoundError as e:
    #         print(f"Error: {e}")
    #     except Exception as e:
    #         print(f"An unexpected error occurred: {e}")
    #
    # if __name__ == "__main__":
    #     main()

log_path = Path(LOG_FILE_PATH)
print(f"Attempting to parse log file: {log_path.resolve()}")
data_df = parse_log_file(log_path)
print(f"Successfully parsed {len(data_df)} records.")

jitter = np.diff(data_df.index).astype(float)*1e-6 - 1
std = np.std(jitter)
med = np.median(jitter)
p99 = np.percentile(np.abs(jitter), 99)
mn = np.mean(jitter)
mi = np.min(jitter)
ma = np.max(jitter)
title = f"min={mi:3.2f} mean={mn:3.2f} std={std:3.2f} med={med:3.2f} p99={p99:3.2f} max={ma:3.2f}"
plt.hist(jitter,bins=np.linspace(-1,4,200),log=True)
plt.xlabel("time [ms]")
plt.title(title)
now = datetime.datetime.now()
now_str = now.isoformat()
plt.savefig(f"jitter_{now_str}.png")