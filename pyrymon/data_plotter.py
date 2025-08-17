import pyqtgraph as pg
from PyQt6.QtWidgets import QGraphicsTextItem
from collections import deque
import numpy as np
import time

class DataPlotter(pg.PlotWidget):
    """
    A widget for plotting time-series data with zooming and downsampling.
    """
    def __init__(self, parent=None, history_seconds=30):
        super().__init__(parent)
        self.history_seconds = history_seconds
        self.curves = {}
        self.data = {}

        self.getPlotItem().setLabel('bottom', 'Time', 's')
        self.getPlotItem().setLabel('left', 'Value')
        self.getPlotItem().showGrid(x=True, y=True)
        self.setDownsampling(mode='peak')
        self.setClipToView(True)

    def add_series(self, name, color='w'):
        """Adds a new data series to the plot."""
        if name not in self.curves:
            pen = pg.mkPen(color=color, width=2)
            self.curves[name] = self.plot(pen=pen, name=name)
            self.data[name] = deque()

    def update_data(self, timestamp, all_values, active_metrics):
        """Updates the plot with new data points."""
        for name, params in active_metrics.items():
            offset = params['offset']
            if offset < len(all_values):
                value = all_values[offset]
                self.data[name].append((timestamp, value))

        now = time.time()
        for name in self.data:
            while self.data[name] and self.data[name][0][0] < now - self.history_seconds:
                self.data[name].popleft()

        for name, curve in self.curves.items():
            if name in active_metrics and self.data[name]:
                x, y = zip(*self.data[name])
                curve.setData(x=np.array(x), y=np.array(y))
            else:
                curve.clear()