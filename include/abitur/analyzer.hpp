// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace abitur {
struct Finding {
    std::string rule;
    std::string severity; // error, warning, note
    std::string symbol;
    std::string message;
    std::vector<std::string> evidence;
    std::vector<std::string> consumers;
};
struct Consumer {
    std::string path;
    std::vector<std::string> imports;
    std::vector<std::string> affected;
};
struct Report {
    std::string old_path;
    std::string new_path;
    std::string scope;
    std::string decision; // break, review, pass
    std::string reason;
    std::vector<Finding> findings;
    std::vector<Consumer> consumers;
    std::vector<std::string> limitations;
    int exit_code() const;
};
struct Options {
    std::filesystem::path old_path;
    std::filesystem::path new_path;
    std::vector<std::filesystem::path> consumers;
};
// Throws std::runtime_error on invalid/unsupported inputs or incomplete DWARF.
Report compare(const Options& options);
std::string json(const Report& report);
std::string sarif(const Report& report);
std::string text(const Report& report);
void write_file(const std::filesystem::path& path, const std::string& contents);
} // namespace abitur
