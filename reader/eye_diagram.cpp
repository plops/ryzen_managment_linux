#include "eye_diagram.hpp"

EyeDiagramStorage::EyeDiagramStorage(size_t n_sensors, size_t reserve_per_bin) {
    bins.resize(NUM_BINS);
    for (auto &bin : bins) {
        bin.resize(n_sensors);
        for (auto &vec : bin) {
            vec.reserve(reserve_per_bin);
        }
    }
}

void EyeDiagramStorage::clear() {
    event_count = 0;
    for (auto &bin : bins) {
        for (auto &vec : bin) {
            vec.clear(); // keep capacity
        }
    }
}

void EyeDiagramStorage::add_sample(int bin_index, size_t sensor_index, float value) {
    if (bin_index < 0 || bin_index >= NUM_BINS) return;
    if (sensor_index >= bins[bin_index].size()) return;
    bins[bin_index][sensor_index].push_back(value);
}

