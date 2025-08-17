import time
import struct
from PyQt6.QtCore import QThread, pyqtSignal
import numpy as np

class PMTableReader(QThread):
    """
    Reads the pm_table file at a specified frequency in a separate thread.
    """
    data_ready = pyqtSignal(float, np.ndarray)
    error = pyqtSignal(str)

    def __init__(self, file_path, frequency=1000):
        super().__init__()
        self.file_path = file_path
        self._is_running = True
        self.interval = 1.0 / frequency

    def run(self):
        """
        Continuously reads the pm_table file and emits the data.
        """
        try:
            with open(self.file_path, "rb") as f:
                while self._is_running:
                    start_time = time.perf_counter()

                    # Seek to the beginning and read the entire file
                    f.seek(0)
                    binary_data = f.read()

                    if not binary_data:
                        time.sleep(self.interval)
                        continue

                    # Unpack the binary data into a list of floats
                    num_floats = len(binary_data) // 4
                    float_values = struct.unpack(f'<{num_floats}f', binary_data)

                    self.data_ready.emit(time.time(), np.array(float_values, dtype=np.float32))

                    # Maintain the desired frequency
                    elapsed = time.perf_counter() - start_time
                    sleep_time = self.interval - elapsed
                    if sleep_time > 0:
                        time.sleep(sleep_time)

        except FileNotFoundError:
            self.error.emit(f"The file '{self.file_path}' was not found.")
        except Exception as e:
            self.error.emit(f"An error occurred while reading the file: {e}")

    def stop(self):
        """Stops the reading loop."""
        self._is_running = False