#ifndef GUI_COMPONENTS_HPP
#define GUI_COMPONENTS_HPP

#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include "eye_diagram.hpp"

/**
 * @brief Holds the render-ready data for a single eye diagram plot.
 */
struct EyePlotData {
    int original_sensor_index;
    std::vector<float> x_data; // Time in ms
    std::vector<float> y_data; // Median values
};

/**
 * @brief Manages the data preparation for the GUI.
 *
 * This class is responsible for periodically processing the raw EyeDiagramStorage
 * into a render-ready format (calculating medians). This avoids doing heavy
 * computation in the render loop.
 */
class GuiDataCache {
public:
    std::vector<EyePlotData> plot_data;
    std::mutex data_mutex;
    int window_before_ms = 0;
    int window_after_ms = 0;

    /**
     * @brief Updates the cached plot data from the raw eye storage.
     *
     * This should be called periodically, but not necessarily on every frame.
     * It locks the eye_storage, calculates medians, and updates its internal cache.
     * @param eye_storage The raw data source from the measurement thread.
     */
    void update(const EyeDiagramStorage &eye_storage);
};

/**
 * @brief Renders the main GUI window.
 *
 * @param cache The data cache containing render-ready plot data.
 * @param n_total_sensors Total number of sensors in the pm_table.
 * @param interesting_indices A map from original sensor index to its position in the cache.
 * @param experiment_status A string describing the current state of the experiment.
 * @param manual_mode Atomic bool to control auto vs manual mode.
 * @param manual_core_to_test Atomic int for the user-selected core in manual mode.
 * @param num_hardware_threads Total number of threads for the core selector range.
 */
void render_gui(GuiDataCache &cache, int n_total_sensors, const std::vector<int>& interesting_indices, const std::string& experiment_status,
                std::atomic<bool>& manual_mode, std::atomic<int>& manual_core_to_test, int num_hardware_threads);


#endif // GUI_COMPONENTS_HPP
