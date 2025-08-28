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
#
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

# --- Eye diagram plot for temperature transitions ---

window_before = 50  # number of samples before rising edge
window_after = 150  # number of samples after rising edge

core_sel = core_ids[1:4]

fig_eye, axes_eye = plt.subplots(len(core_sel), 1, figsize=(10, 3 * len(core_sel)))
if n_cores == 1:
    axes_eye = [axes_eye]

for idx, core_id in enumerate(core_sel):
    core_df = run_df[run_df['core_id'] == core_id].reset_index(drop=True)

#     core_df
#     round  core_id   timestamp_ns  worker_state       v17
# 0         0        4  3491913291292             1  108.4580
# 1         0        4  3491914355153             1  107.0570
# 2         0        4  3491915354747             1  105.6890
# 3         0        4  3491916348591             1  105.6370
# 4         0        4  3491917355146             1  104.7680
# ...     ...      ...            ...           ...       ...
# 1947      0        4  3493860343373             0   95.3795
# 1948      0        4  3493861343437             0   94.8878
# 1949      0        4  3493862343321             0   94.9531
# 1950      0        4  3493863343245             0   94.8944
# 1951      0        4  3493864343599             0   95.2402
# [1952 rows x 5 columns]

    v17 = core_df['v17'].values
    worker_state = core_df['worker_state'].values

    # Find rising (0->1) and falling (1->0) edges
    rising_edges = np.where((worker_state[:-1] == 0) & (worker_state[1:] == 1))[0] + 1
    # rising_edges
    # array([ 151,  301,  451,  601,  751,  901, 1051, 1201, 1351, 1501, 1651,
    #        1801])
    falling_edges = np.where((worker_state[:-1] == 1) & (worker_state[1:] == 0))[0] + 1

    # For each full 0->1->0 sequence, extract v17 trace
    traces = []
    for rise in rising_edges:
        # Find next falling edge after this rising edge
        falls = falling_edges[falling_edges > rise]
        if len(falls) == 0:
            continue
        fall = falls[0]
        # Define window: from (rise - window_before) to (fall + window_after)
        start = max(rise - window_before, 0)
        end = min(fall + window_after, len(v17))
        trace = v17[start:end]
        # Pad if needed to align all traces
        pad_left = rise - window_before - start
        pad_right = (fall + window_after) - end
        trace = np.pad(trace, (pad_left, pad_right), mode='edge')
        traces.append(trace)

    # Align all traces to the rising edge (t=0 at rising edge)
    if traces:
        traces = np.array(traces)
        t = np.arange(-window_before, traces.shape[1] - window_before)
        ax_eye = axes_eye[idx]
        for tr in traces:
            ax_eye.plot(t, tr, color='C0', alpha=0.2)
        ax_eye.plot(t, np.median(traces, axis=0), color='C1', label='Median', linewidth=2)
        ax_eye.set_title(f'Eye Diagram: Core {core_id}')
        ax_eye.set_xlabel('Sample (0 = rising edge)')
        ax_eye.set_ylabel('v17 (Temperature)')
        p10 = np.percentile(core_df['v17'], 5)
        p90 = np.percentile(core_df['v17'], 98)
        ax_eye.set_ylim(p10 - 0.1 * (p90 - p10), p90 + 0.2 * (p90 - p10))
        ax_eye.legend()
    else:
        axes_eye[idx].set_title(f'Eye Diagram: Core {core_id} (no transitions found)')

    # traces
    # array([[ 97.0007,  96.8257,  97.047 , ..., 109.503 , 110.52  , 110.    ],
    #        [ 95.8211,  95.8211,  95.6852, ..., 109.976 , 106.777 , 106.446 ],
    #        [ 95.4317,  95.0878,  95.0325, ..., 109.37  , 105.886 , 105.957 ],
    #        ...,
    #        [ 95.5307,  95.4609,  96.4139, ..., 109.728 , 109.277 , 105.997 ],
    #        [ 95.7724,  95.8428,  95.8428, ..., 110.687 , 106.873 , 105.884 ],
    #        [ 97.5043,  96.6841,  96.6247, ...,  95.2402,  95.2402,  95.2402]],
    #       shape=(12, 275))

plt.tight_layout()
plt.show()



