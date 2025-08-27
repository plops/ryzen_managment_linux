import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import argparse
from pathlib import Path
plt.ion()

df0 = pd.read_csv('cmake-build-release/results/output.csv')
core_id = 2
sensor_name = "v17"
round_id = 0
df = df0[['round','core_id','timestamp_ns','worker_state','v17']]
run_df = df[(df['core_id'] == core_id) & (df['round'] == round_id)].copy()
run_df['prev_state'] = run_df['worker_state'].shift(1)
run_df['v17'].plot()
(run_df['worker_state']*6+60).plot()


# triggers = run_df[(run_df['worker_state'] == 1) & (run_df['prev_state'] == 0)]
# trigger_timestamps_ns = triggers['timestamp_ns'].values
# pre_trigger_ns = pre_trigger_ms * 1e6
# post_trigger_ns = post_trigger_ms * 1e6