# ryzen_pm_monitor/src/main.py
import sys
import time
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout,
    QCheckBox, QGridLayout, QLabel, QMessageBox
)
from PyQt6.QtCore import pyqtSlot
from config_loader import load_config
from pm_table_reader import PMTableReader
from data_plotter import DataPlotter

# Path to the pm_table file. For development, you might want to use a dummy file.
PM_TABLE_PATH = "/sys/kernel/ryzen_smu_drv/pm_table"
# PM_TABLE_PATH = "dummy_pm_table" # <-- Use this for testing without the driver

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Ryzen PM Table Monitor")
        self.setGeometry(100, 100, 1200, 800)

        self.config = load_config()
        if not self.config:
            self.show_error("Could not load or parse config.toml. Exiting.")
            sys.exit(1)

        self.active_metrics = {}

        # Main layout
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)

        # Plotter
        self.plotter = DataPlotter(history_seconds=60)
        main_layout.addWidget(self.plotter)

        # Controls for selecting metrics
        controls_widget = QWidget()
        controls_layout = QGridLayout(controls_widget)
        main_layout.addWidget(controls_widget)

        for i, (name, params) in enumerate(self.config.items()):
            checkbox = QCheckBox(name)
            checkbox.stateChanged.connect(lambda state, n=name, p=params: self.toggle_metric(n, p, state))
            controls_layout.addWidget(checkbox, i // 8, i % 8)

        # Initialize and start the reader thread
        self.reader_thread = PMTableReader(PM_TABLE_PATH, frequency=1000)
        self.reader_thread.data_ready.connect(self.handle_data)
        self.reader_thread.error.connect(self.show_error)
        self.reader_thread.start()

    def toggle_metric(self, name, params, state):
        """Activates or deactivates a metric for plotting."""
        if state:
            self.active_metrics[name] = params
            self.plotter.add_series(name, color=params.get('color', 'w'))
        else:
            if name in self.active_metrics:
                del self.active_metrics[name]

    @pyqtSlot(float, object)
    def handle_data(self, timestamp, values):
        """Receives data from the reader thread and updates the plot."""
        if self.active_metrics:
            self.plotter.update_data(timestamp, values, self.active_metrics)

    @pyqtSlot(str)
    def show_error(self, message):
        """Shows an error message."""
        QMessageBox.critical(self, "Error", message)
        self.reader_thread.stop()

    def closeEvent(self, event):
        """Ensures the reader thread is stopped when closing the window."""
        self.reader_thread.stop()
        self.reader_thread.wait()
        event.accept()

if __name__ == "__main__":
    # Create a dummy pm_table file for testing if the real one doesn't exist
    import os
    import struct
    import numpy as np

    if not os.path.exists(PM_TABLE_PATH):
        print(f"'{PM_TABLE_PATH}' not found. Creating a dummy file for testing.")
        PM_TABLE_PATH = "dummy_pm_table"
        try:
            with open(PM_TABLE_PATH, "wb") as f:
                # Create a file with 200 float values
                dummy_data = np.zeros(200, dtype=np.float32)
                f.write(struct.pack(f'<{len(dummy_data)}f', *dummy_data))
        except IOError as e:
            print(f"Could not create dummy file: {e}")
            sys.exit(1)


    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())