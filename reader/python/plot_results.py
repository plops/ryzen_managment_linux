#!/usr/bin/env python3
"""
Plots results from the measure binary's CSV output.

This script generates two types of plots for each core and round in the data:
1. A time-series plot of all sensor values against the worker state.
2. An "eye diagram" for each sensor, showing the response aligned to the
   workload's rising edge (worker_state 0 -> 1).
"""

import argparse
import os
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

def plot_time_series(df, core_id, round_id, output_dir):
    """Plots sensor values and worker state over time for a given core and round."""
    print(f"Plotting time series for core {core_id}, round {round_id}...")
    
    fig, ax1 = plt.subplots(figsize=(15, 8))

    # Time axis
    time_s = (df['timestamp_ns'] - df['timestamp_ns'].iloc[0]) / 1e9
    ax1.set_xlabel('Time (s)')
    ax1.set_ylabel('Sensor Value')
    
    sensor_cols = [col for col in df.columns if col.startswith('v')]
    for col in sensor_cols:
        ax1.plot(time_s, df[col], label=col, alpha=0.8)
    
    ax1.legend(loc='upper left')
    ax1.grid(True, linestyle='--', alpha=0.6)

    # Worker state on a secondary y-axis
    ax2 = ax1.twinx()
    ax2.set_ylabel('Worker State', color='r')
    ax2.plot(time_s, df['worker_state'], 'r-', label='worker_state', alpha=0.5, drawstyle='steps-post')
    ax2.tick_params(axis='y', labelcolor='r')
    ax2.set_ylim(-0.1, 1.1)

    plt.title(f'Time Series for Core {core_id}, Round {round_id}')
    fig.tight_layout()
    
    output_path = os.path.join(output_dir, f'timeseries_core{core_id}_round{round_id}.png')
    plt.savefig(output_path, dpi=150)
    plt.close(fig)

def plot_eye_diagrams(df, core_id, round_id, output_dir, window_ms=100):
    """
    Plots eye diagrams for each sensor, aligned to worker state rising edge.
    
    Args:
        df (pd.DataFrame): DataFrame for a single core and round.
        core_id (int): The core ID.
        round_id (int): The round ID.
        output_dir (str): Directory to save plots.
        window_ms (int): The time window in milliseconds around the rising edge.
    """
    print(f"Plotting eye diagrams for core {core_id}, round {round_id}...")
    
    # Find rising edges (0 -> 1 transitions)
    rising_edges = df[(df['worker_state'].diff() == 1)].index
    
    if len(rising_edges) == 0:
        print(f"No rising edges found for core {core_id}, round {round_id}. Skipping eye diagrams.")
        return

    sensor_cols = [col for col in df.columns if col.startswith('v')]
    time_s = (df['timestamp_ns'] - df['timestamp_ns'].iloc[0]) / 1e9
    
    # Determine window size in samples (assuming ~1ms sampling)
    pre_samples = window_ms // 2
    post_samples = window_ms // 2

    for sensor in sensor_cols:
        fig, ax = plt.subplots(figsize=(10, 6))
        
        all_traces = []
        for edge_idx in rising_edges:
            start_idx = edge_idx - pre_samples
            end_idx = edge_idx + post_samples
            
            if start_idx < 0 or end_idx >= len(df):
                continue

            trace = df[sensor].iloc[start_idx:end_idx].values
            if len(trace) == (pre_samples + post_samples):
                all_traces.append(trace)
                time_axis_ms = np.arange(-pre_samples, post_samples)
                ax.plot(time_axis_ms, trace, 'b-', alpha=0.05)

        if not all_traces:
            print(f"Could not extract any full traces for sensor {sensor}. Skipping plot.")
            plt.close(fig)
            continue

        # Plot median and quantiles
        all_traces = np.array(all_traces)
        median_trace = np.median(all_traces, axis=0)
        q25 = np.quantile(all_traces, 0.25, axis=0)
        q75 = np.quantile(all_traces, 0.75, axis=0)
        
        time_axis_ms = np.arange(-pre_samples, post_samples)
        ax.plot(time_axis_ms, median_trace, 'r-', lw=2, label='Median')
        ax.fill_between(time_axis_ms, q25, q75, color='r', alpha=0.3, label='IQR (25-75%)')

        ax.axvline(0, color='k', linestyle='--', label='Workload Start')
        ax.set_xlabel('Time from rising edge (ms)')
        ax.set_ylabel(f'Sensor {sensor} Value')
        ax.set_title(f'Eye Diagram for {sensor} (Core {core_id}, Round {round_id})')
        ax.grid(True, linestyle='--', alpha=0.6)
        ax.legend()
        
        output_path = os.path.join(output_dir, f'eye_{sensor}_core{core_id}_round{round_id}.png')
        plt.savefig(output_path, dpi=150)
        plt.close(fig)

def main():
    parser = argparse.ArgumentParser(description="Plot results from the measurement tool's CSV output.")
    parser.add_argument('input_file', type=str, nargs='?', default='../cmake-build-release/results/output.csv',
                        help='Path to the input CSV file (default: results/output.csv)')
    parser.add_argument('-o', '--output-dir', type=str, default='results/plots',
                        help='Directory to save plots (default: results/plots)')
    args = parser.parse_args()

    if not os.path.exists(args.input_file):
        print(f"Error: Input file not found at '{args.input_file}'")
        return

    os.makedirs(args.output_dir, exist_ok=True)
    print(f"Reading data from {args.input_file}...")
    df = pd.read_csv(args.input_file)

    for (round_id, core_id), group_df in df.groupby(['round', 'core_id']):
        group_df = group_df.sort_values('timestamp_ns').reset_index(drop=True)
        
        # Generate Time Series Plot
        plot_time_series(group_df, core_id, round_id, args.output_dir)
        
        # Generate Eye Diagram Plots
        plot_eye_diagrams(group_df, core_id, round_id, args.output_dir)

    print(f"\nAll plots saved to '{args.output_dir}'")

if __name__ == '__main__':
    main()

