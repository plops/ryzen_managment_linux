#!/usr/bin/env python3
"""
Plots results from the measure binary's CSV output interactively.

This script is designed for cell-by-cell execution in an interactive
environment like IPython or a Jupyter notebook. It loads data and then
generates plots for a single specified core and round.
"""

import argparse
import os
import matplotlib
matplotlib.use('Qt5Agg')  # Set backend before importing pyplot
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns

# %% --- Configuration and Setup ---
sns.set_theme(style="darkgrid")

parser = argparse.ArgumentParser(description="Plot results from the measurement tool's CSV output.")
parser.add_argument('input_file', type=str, nargs='?', default='../cmake-build-release/results/output.csv',
                    help='Path to the input CSV file (default: results/output.csv)')
parser.add_argument('-c', '--core', type=int, default=0, help='Core ID to plot')
parser.add_argument('-r', '--round', type=int, default=0, help='Round to plot')
parser.add_argument('-o', '--output-dir', type=str, default='results/plots',
                    help='Directory to save plots (default: results/plots)')
args = parser.parse_args()

if not os.path.exists(args.input_file):
    print(f"Error: Input file not found at '{args.input_file}'")
    exit()

os.makedirs(args.output_dir, exist_ok=True)

# %% --- Load and Filter Data ---
print(args.input_file)
print(f"Reading data from {args.input_file}...")
df = pd.read_csv(args.input_file)

print(f"Filtering for Core ID: {args.core}, Round: {args.round}")
group_df = df[(df['core_id'] == args.core) & (df['round'] == args.round)].copy()
group_df = group_df.sort_values('timestamp_ns').reset_index(drop=True)

if group_df.empty:
    print("No data found for the specified core and round. Exiting.")
    exit()

# %% --- Time Series Plot ---
print(f"Plotting time series for core {args.core}, round {args.round}...")
fig, ax1 = plt.subplots(figsize=(15, 8))

# Time axis
group_df['time_s'] = (group_df['timestamp_ns'] - group_df['timestamp_ns'].iloc[0]) / 1e9

sensor_cols = [col for col in group_df.columns if col.startswith('v')]

# Melt the dataframe for seaborn
df_long = group_df.melt(id_vars=['time_s'], value_vars=sensor_cols, var_name='sensor', value_name='value')

# Plot sensors with seaborn
sns.lineplot(data=df_long, x='time_s', y='value', hue='sensor', ax=ax1, alpha=0.8)

ax1.set_xlabel('Time (s)')
ax1.set_ylabel('Sensor Value')
ax1.legend(loc='upper left')

# Worker state on a secondary y-axis
ax2 = ax1.twinx()
ax2.set_ylabel('Worker State', color='r')
sns.lineplot(data=group_df, x='time_s', y='worker_state', color='r', ax=ax2, label='worker_state', alpha=0.5, drawstyle='steps-post')
ax2.tick_params(axis='y', labelcolor='r')
ax2.set_ylim(-0.1, 1.1)
ax2.legend(loc='upper right')

plt.title(f'Time Series for Core {args.core}, Round {args.round}')
fig.tight_layout()

output_path = os.path.join(args.output_dir, f'timeseries_core{args.core}_round{args.round}.png')
plt.savefig(output_path, dpi=150)
print(f"Saved time series plot to {output_path}")
plt.show()


# %% --- Eye Diagram Plots ---
print(f"Plotting eye diagrams for core {args.core}, round {args.round}...")
window_ms = 100

# Find rising edges (0 -> 1 transitions)
rising_edges = group_df[(group_df['worker_state'].diff() == 1)].index

if len(rising_edges) == 0:
    print(f"No rising edges found. Skipping eye diagrams.")
else:
    # Determine window size in samples (assuming ~1ms sampling)
    pre_samples = window_ms // 2
    post_samples = window_ms // 2

    for sensor in sensor_cols:
        all_traces = []
        for edge_idx in rising_edges:
            start_idx = edge_idx - pre_samples
            end_idx = edge_idx + post_samples
            
            if start_idx < 0 or end_idx >= len(group_df):
                continue

            trace = group_df[sensor].iloc[start_idx:end_idx]
            if len(trace) == (pre_samples + post_samples):
                time_axis_ms = np.arange(-pre_samples, post_samples)
                trace_df = pd.DataFrame({
                    'time_ms': time_axis_ms,
                    'value': trace.values
                })
                all_traces.append(trace_df)

        if not all_traces:
            print(f"Could not extract any full traces for sensor {sensor}. Skipping plot.")
            continue

        # Concatenate all traces into a single long-form DataFrame
        traces_df = pd.concat(all_traces)

        # Create plot
        fig, ax = plt.subplots(figsize=(10, 6))

        # Plot all traces with a low alpha
        sns.lineplot(data=traces_df, x='time_ms', y='value', units=traces_df.index, estimator=None, lw=1, alpha=0.05, color='b', ax=ax)
        # Overlay the mean and 95% CI
        sns.lineplot(data=traces_df, x='time_ms', y='value', color='r', lw=2, label='Mean & 95% CI', ax=ax)

        ax.axvline(0, color='k', linestyle='--', label='Workload Start')
        ax.set_xlabel('Time from rising edge (ms)')
        ax.set_ylabel(f'Sensor {sensor} Value')
        ax.set_title(f'Eye Diagram for {sensor} (Core {args.core}, Round {args.round})')
        ax.legend()
        
        output_path = os.path.join(args.output_dir, f'eye_{sensor}_core{args.core}_round{args.round}.png')
        plt.savefig(output_path, dpi=150)
        print(f"Saved eye diagram to {output_path}")
        plt.show()

print("\nScript finished.")
