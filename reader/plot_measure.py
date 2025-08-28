import pandas as pd
import numpy as np
import matplotlib
# configure matplotlib for interactive use with qt6
matplotlib.use('QtAgg')
import matplotlib.pyplot as plt
import seaborn as sns
import argparse
from pathlib import Path
plt.ion()

df0 = pd.read_csv('cmake-build-release/results/output.csv')
sensor_name = "v17"
round_id = 0
df = df0[['round','core_id','timestamp_ns','worker_state','v17']]

# df
# round  core_id   timestamp_ns  worker_state       v17
# 0          0        1  3485531564538             1   92.8704
# 1          0        1  3485532628149             1  110.8580
# 2          0        1  3485533627753             1  113.7740
# 3          0        1  3485534620736             1  114.1450
# 4          0        1  3485535628041             1  111.7980
# ...      ...      ...            ...           ...       ...
# 60501      0       31  3551309111015             0   95.3040
# 60502      0       31  3551310111550             0   97.6849
# 60503      0       31  3551311110933             0   95.9827
# 60504      0       31  3551312111007             0   96.6330
# 60505      0       31  3551313111052             0   95.9639
# [60506 rows x 5 columns]


run_df = df[df['round'] == round_id].copy()

# run_df
# round  core_id   timestamp_ns  worker_state       v17
# 0          0        1  3485531564538             1   92.8704
# 1          0        1  3485532628149             1  110.8580
# 2          0        1  3485533627753             1  113.7740
# 3          0        1  3485534620736             1  114.1450
# 4          0        1  3485535628041             1  111.7980
# ...      ...      ...            ...           ...       ...
# 60501      0       31  3551309111015             0   95.3040
# 60502      0       31  3551310111550             0   97.6849
# 60503      0       31  3551311110933             0   95.9827
# 60504      0       31  3551312111007             0   96.6330
# 60505      0       31  3551313111052             0   95.9639
# [60506 rows x 5 columns]

core_ids = sorted(run_df['core_id'].unique())
n_cores = len(core_ids)

# fig, axes = plt.subplots(n_cores, 1, figsize=(10, 3 * n_cores))  # removed sharex=True
# if n_cores == 1:
#     axes = [axes]

# for idx, core_id in enumerate(core_ids):
#     core_df = run_df[run_df['core_id'] == core_id]
#     ax = axes[idx]
#     # Plot v17 using seaborn
#     sns.lineplot(data=core_df, x='timestamp_ns', y='v17', ax=ax, label='v17')
#     # Compute percentiles for scaling
#     p10 = np.percentile(core_df['v17'], 10)
#     p90 = np.percentile(core_df['v17'], 90)
#     # Overlay scaled worker_state
#     scaled_worker_state = core_df['worker_state'] * (p90 - p10) + p10
#     ax.plot(core_df['timestamp_ns'], scaled_worker_state, label='worker_state (scaled)', alpha=0.7)
#     ax.set_title(f'Core {core_id}')
#     ax.legend(loc='upper right')
#     ax.set_ylabel('v17')
#     ax.set_ylim(p10 - 0.1 * (p90 - p10), p90 + 0.1 * (p90 - p10))
# axes[-1].set_xlabel('timestamp_ns')
# plt.tight_layout()
# plt.show()

# --- Convert timestamp to a relative time in seconds ---
run_df['time_s'] = (run_df['timestamp_ns'] - run_df['timestamp_ns'].min()) / 1e9

core_ids = sorted(run_df['core_id'].unique())
n_cores = len(core_ids)

# --- Eye diagram plot for temperature transitions ---

# --- Define window in seconds instead of samples ---
window_before_s = 0.05  # 50 ms before
window_after_s = 0.15   # 150 ms after
sampling_period_s = 0.001 # Resample to a uniform 1ms grid

core_sel = core_ids[1:4]

fig_eye, axes_eye = plt.subplots(len(core_sel), 1, figsize=(10, 3 * len(core_sel)))
if n_cores == 1:
    axes_eye = [axes_eye]

for idx, core_id in enumerate(core_sel):
    core_df = run_df[run_df['core_id'] == core_id].copy()

    # Get the time and sensor values
    time_s = core_df['time_s'].values
    v17 = core_df['v17'].values
    worker_state = core_df['worker_state'].values

    # Find rising (0->1) and falling (1->0) edges
    rising_edge_indices = np.where((worker_state[:-1] == 0) & (worker_state[1:] == 1))[0] + 1

    # --- Create a new uniform time grid for interpolation ---
    # This grid will be centered around the rising edge event (t=0)
    # The total duration of the grid is window_before_s + window_after_s
    uniform_time_grid = np.arange(-window_before_s, window_after_s, sampling_period_s)

    resampled_traces = []

    for rise_idx in rising_edge_indices:
        # Get the exact time of the rising edge
        rise_time = time_s[rise_idx]

        # Define the time window for this specific event
        start_time = rise_time - window_before_s
        end_time = rise_time + window_after_s

        # Select the original data points that fall within this window
        window_mask = (time_s >= start_time) & (time_s <= end_time)

        # Get the timestamps and sensor values within that window
        time_window = time_s[window_mask]
        v17_window = v17[window_mask]

        # --- Interpolate the data onto the uniform time grid ---
        # We shift the time window so the rising edge is at t=0 for interpolation
        # The np.interp function requires the x-points to be increasing, which they already are.
        if len(time_window) > 1: # Need at least 2 points to interpolate
            resampled_trace = np.interp(uniform_time_grid, time_window - rise_time, v17_window)
            resampled_traces.append(resampled_trace)

    # Align all traces to the rising edge (t=0 at rising edge)
    if resampled_traces:
        # Convert list of traces to a 2D numpy array
        resampled_traces = np.array(resampled_traces)

        ax_eye = axes_eye[idx]
        # Plot each individual resampled trace
        for trace in resampled_traces:
            ax_eye.plot(uniform_time_grid, trace, color='C0', alpha=0.2)

        # --- Compute the median on the correctly aligned data ---
        median_trace = np.median(resampled_traces, axis=0)
        ax_eye.plot(uniform_time_grid, median_trace, color='C1', label='Median', linewidth=2)

        ax_eye.set_title(f'Eye Diagram: Core {core_id}')
        ax_eye.set_xlabel('Time (s) relative to rising edge')
        ax_eye.set_ylabel('v17 (Temperature)')

        p10 = np.percentile(core_df['v17'], 5)
        p90 = np.percentile(core_df['v17'], 98)
        ax_eye.set_ylim(p10 - 0.1 * (p90 - p10), p90 + 0.2 * (p90 - p10))
        ax_eye.legend()
    else:
        axes_eye[idx].set_title(f'Eye Diagram: Core {core_id} (no transitions found)')

plt.tight_layout()
plt.show()
