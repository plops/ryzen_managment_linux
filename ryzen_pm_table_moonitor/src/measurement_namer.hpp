#pragma once

#include <string>
#include <map>
#include <mutex>
#include <optional>
#include <algorithm> // For std::toupper
#include <stdexcept> // For std::stoi exceptions
#include <fmt/format.h> // For fmt::format, assuming it's available or adapt to std::format in C++20

// NEW: A class to manage loading, saving, and accessing measurement names
class MeasurementNamer {
private:
    std::string filepath_;
    std::map<std::string, std::string> names_;
    std::mutex mutex_;

    // Helper to convert chess index string to a decimal index (0-indexed)
    static std::optional<int> from_chess_index(const std::string& chess_index) {
        if (chess_index.length() < 2 || !std::isalpha(chess_index[0])) {
            return std::nullopt;
        }
        char col_char = static_cast<char>(std::toupper(chess_index[0]));
        if (col_char < 'A' || col_char > 'H') { // Assuming columns A-H
            return std::nullopt;
        }
        int col = col_char - 'A';

        try {
            // Find the start of the numeric part of the row
            size_t row_str_start = 1;
            while(row_str_start < chess_index.length() && !std::isdigit(chess_index[row_str_start])) {
                row_str_start++;
            }
            if (row_str_start == chess_index.length()) { // No digits found
                return std::nullopt;
            }

            int row = std::stoi(chess_index.substr(row_str_start));
            return row * 8 + col; // Assuming 8 columns per row
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

public:
    explicit MeasurementNamer(std::string filepath);

    // Convert an integer index (0-indexed) to its chess notation (e.g., 0 -> "A0", 8 -> "A1")
    static std::string to_chess_index(int index) {
        if (index < 0) return "Invalid";
        char col = 'A' + (index % 16);
        int row = (index / 16);
        return fmt::format("{}{}", col, row);
    }

    // Parses an index string.
    // If it starts with a digit, it's treated as a decimal index.
    // Otherwise, it's treated as a chess index.
    static std::optional<int> parse_index(const std::string& index_str) {
        if (index_str.empty()) {
            return std::nullopt;
        }
        if (std::isdigit(index_str[0])) {
            try {
                return std::stoi(index_str);
            } catch (const std::exception&) {
                return std::nullopt;
            }
        } else {
            return from_chess_index(index_str);
        }
    }

    void load_from_file();
    void save_to_file();
    std::optional<std::string> get_name(int index);
    void set_name(int index, const std::string& name);
};