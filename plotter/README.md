run with
```
uv run python main.py
```


example output

```

plotter $ uv run main.py 
Attempting to parse log file: /home/kiel/stage/ryzen_managment_linux/plotter/pm_table_log.bin
Successfully parsed 4315 records.

Displaying first 5 rows of data:
                               socket_power  cpu_power   soc_power  cpu_temp  total_core_power  avg_core_freq  peak_core_freq
timestamp                                                                                                                    
2025-08-17 14:13:30.146001462           0.0   1.238305  147.839355  0.559079      17519.047729     377.262422      500.000031
2025-08-17 14:13:30.147056310           0.0   1.238305  147.839355  0.559079      17519.047729     377.262422      500.000031
2025-08-17 14:13:30.148063304           0.0   1.238305  147.839355  0.559079      17519.047729     377.262422      500.000031
2025-08-17 14:13:30.149062897           0.0   1.238305  147.839355  0.559079      17519.047729     377.262422      500.000031
2025-08-17 14:13:30.150062930           0.0   1.238305  147.839355  0.559079      17519.047729     377.262422      500.000031
```