// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <cstdint>
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

struct ElfSection {
    std::string name;
    std::string type;
    uint64_t address = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint64_t alignment = 0;
    std::string flags;
};

struct ElfSegment {
    std::string type;
    uint64_t vaddr = 0;
    uint64_t paddr = 0;
    uint64_t file_size = 0;
    uint64_t mem_size = 0;
    uint64_t align = 0;
    std::string flags;
};

struct ElfSymbolInfo {
    std::string name;
    std::string demangled;
    std::string type;
    std::string binding;
    std::string visibility;
    uint64_t size = 0;
    uint64_t address = 0;
    std::string signature;
};

struct ElfMemberLayout {
    std::string name;
    std::string type;
    uint64_t offset = 0;
    uint64_t size = 0;
    bool is_padding = false;
};

struct ElfTypeLayout {
    std::string name;
    std::string kind;
    uint64_t byte_size = 0;
    uint64_t alignment = 0;
    std::vector<ElfMemberLayout> members;
};

struct ElfAnalysis {
    std::string path;
    std::string file_type;
    std::string machine;
    std::string elf_class;
    std::string endianness;
    std::string osabi;
    uint8_t abi_version = 0;
    uint64_t entry_point = 0;
    std::string soname;
    std::string rpath;
    std::string runpath;
    std::vector<std::string> needed_libraries;
    std::vector<std::string> security_features;
    std::vector<ElfSegment> segments;
    std::vector<ElfSection> sections;
    std::vector<ElfSymbolInfo> exports;
    std::vector<std::string> imports;
    std::vector<ElfTypeLayout> type_layouts;
    std::vector<std::string> warnings;
};

using BinaryAnalysis = ElfAnalysis;
using BinarySection = ElfSection;
using BinarySegment = ElfSegment;
using BinarySymbolInfo = ElfSymbolInfo;
using BinaryMemberLayout = ElfMemberLayout;
using BinaryTypeLayout = ElfTypeLayout;

struct Report {
    std::string old_path;
    std::string new_path;
    std::string scope;
    std::string decision; // break, review, pass
    std::string reason;
    std::vector<Finding> findings;
    std::vector<Consumer> consumers;
    std::vector<std::string> limitations;
    std::optional<ElfAnalysis> elf_analysis;
    int exit_code() const;
};

struct Options {
    std::filesystem::path old_path;
    std::filesystem::path new_path;
    std::vector<std::filesystem::path> consumers;
    bool analyze_mode = false;
};

// Throws std::runtime_error on invalid/unsupported inputs or incomplete DWARF.
Report compare(const Options& options);
Report analyze(const Options& options);
std::string json(const Report& report);
std::string sarif(const Report& report);
std::string text(const Report& report);
void write_file(const std::filesystem::path& path, const std::string& contents);
} // namespace abitur
