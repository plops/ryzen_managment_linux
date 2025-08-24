#include "measurement_namer.hpp"
#include <fstream>
#include <spdlog/spdlog.h>
#include "toml++/toml.hpp" // Required for TOML parsing

MeasurementNamer::MeasurementNamer(std::string filepath) : filepath_(std::move(filepath)) {
    load_from_file();
}

void MeasurementNamer::load_from_file() {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        toml::table tbl = toml::parse_file(filepath_);
        if (auto names_table = tbl["names"].as_table()) {
            for (auto&& [key, val] : *names_table) {
                if (val.is_string()) {
                    names_[std::string(key)] = val.as_string()->get();
                }
            }
            spdlog::info("Successfully loaded {} names from {}", names_.size(), filepath_);
        }
    } catch (const toml::parse_error& err) {
        spdlog::warn("Could not load names file '{}': {}. A new one will be created on save.", filepath_, err.description());
    } catch (const std::exception& e) {
        spdlog::error("Error loading names file '{}': {}", filepath_, e.what());
    }
}

void MeasurementNamer::save_to_file() {
    std::lock_guard<std::mutex> lock(mutex_);
    toml::table names_table;
    for (const auto& [key, val] : names_) {
        names_table.insert(key, val);
    }

    toml::table root_table;
    root_table.insert("names", names_table);

    std::ofstream file(filepath_);
    if (file.is_open()) {
        file << root_table;
        spdlog::info("Saved names to {}", filepath_);
    } else {
        spdlog::error("Failed to open {} for writing.", filepath_);
    }
}

std::optional<std::string> MeasurementNamer::get_name(const std::string& chess_index_str) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = names_.find(chess_index_str);
    if (it != names_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void MeasurementNamer::set_name(const std::string& chess_index_str, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (name.empty()) {
        names_.erase(chess_index_str);
    } else {
        names_[chess_index_str] = name;
    }
}