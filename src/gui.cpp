// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "abitur/analyzer.hpp"
#include "abitur/desktop/window.hpp"
#include "abitur/version.hpp"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr ImVec4 accent{0.42f, 0.84f, 0.71f, 1};
constexpr ImVec4 muted{0.50f, 0.56f, 0.62f, 1};
constexpr ImVec4 red{1.0f, 0.43f, 0.41f, 1};
constexpr ImVec4 amber{0.96f, 0.73f, 0.38f, 1};
constexpr ImVec4 primary_button{0.17f, 0.42f, 0.35f, 1};
#if defined(__APPLE__)
constexpr const char* mod_key = "Cmd";
#else
constexpr const char* mod_key = "Ctrl";
#endif
using Path = std::array<char, 4096>;

template <size_t N> void assign(std::array<char, N>& dest, const std::string& value) {
    std::snprintf(dest.data(), N, "%s", value.c_str());
}
ImVec4 color(const std::string& severity) {
    return severity == "error" ? red : severity == "warning" ? amber : accent;
}
void label(const char* value) {
    ImGui::TextColored(muted, "%s", value);
}
void wrapped(const std::string& value) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}
void wrapped_muted(const std::string& value) {
    ImGui::PushStyleColor(ImGuiCol_Text, muted);
    wrapped(value);
    ImGui::PopStyleColor();
}
bool primary(const char* text, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, primary_button);
    bool pressed = ImGui::Button(text, size);
    ImGui::PopStyleColor();
    return pressed;
}
bool file_exists(const char* path) {
    std::error_code ec;
    return fs::exists(path, ec);
}
fs::path home_directory() {
    if (const char* home = std::getenv("HOME"); home && *home)
        return home;
    if (const char* home = std::getenv("USERPROFILE"); home && *home)
        return home;
    return fs::current_path();
}
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
// Search field with a hint and an inline clear button. Cmd/Ctrl+F focuses the
// visible search field of the active panel.
void search_box(ImGuiTextFilter& filter, const char* id, const char* hint, float width) {
    ImGui::PushID(id);
    if (!ImGui::IsAnyItemActive() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputTextWithHint("##search", hint, filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf)))
        filter.Build();
    ImGui::SetItemTooltip("%s+F to focus", mod_key);
    if (filter.IsActive()) {
        ImGui::SameLine(0, 4);
        if (ImGui::Button("x"))
            filter.Clear();
        ImGui::SetItemTooltip("Clear search");
    }
    ImGui::PopID();
}
void key_value(const char* key, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    label(key);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value.c_str());
}
std::string hex(uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "0x%llx", static_cast<unsigned long long>(value));
    return buffer;
}

// Filter results are cached until the query or the report changes, so large
// symbol tables stay responsive.
struct FilteredList {
    ImGuiTextFilter filter;
    std::vector<int> rows;
    std::string query;
    int generation = -1;
    template <typename Text> const std::vector<int>& update(int count, int gen, Text&& text) {
        if (gen != generation || query != filter.InputBuf) {
            generation = gen;
            query = filter.InputBuf;
            rows.clear();
            for (int i = 0; i < count; ++i)
                if (!filter.IsActive() || filter.PassFilter(text(i).c_str()))
                    rows.push_back(i);
        }
        return rows;
    }
};

struct Browser {
    struct Entry {
        std::string name;
        bool directory;
    };
    bool request = false;
    int destination = 0;
    std::string title;
    fs::path directory = fs::current_path() == fs::path("/") ? home_directory() : fs::current_path();
    Path location{};
    Path selection{};
    std::string error;
    std::vector<Entry> entries;
    bool stale = true;

    void navigate(const fs::path& target) {
        std::error_code ec;
        if (!fs::is_directory(target, ec)) {
            error = "Folder not found: " + target.string();
            return;
        }
        directory = fs::absolute(target, ec).lexically_normal();
        if (directory.has_relative_path() && directory.filename().empty())
            directory = directory.parent_path();
        assign(location, directory.string());
        selection.fill(0);
        error.clear();
        stale = true;
    }
    void open(int target, const char* heading, const char* current) {
        destination = target;
        title = heading;
        request = true;
        std::error_code ec;
        fs::path path(current);
        if (!path.empty() && fs::exists(path, ec))
            navigate(fs::is_directory(path, ec) ? path : fs::absolute(path, ec).parent_path());
        else
            navigate(directory);
        if (!path.empty() && fs::is_regular_file(path, ec))
            assign(selection, path.filename().string());
    }
    void refresh() {
        stale = false;
        entries.clear();
        std::error_code ec;
        fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied,
                                        ec),
            end;
        while (!ec && iterator != end) {
            auto name = iterator->path().filename().string();
            std::error_code type_error;
            if (!name.starts_with('.'))
                entries.push_back({name, iterator->is_directory(type_error)});
            iterator.increment(ec);
        }
        if (ec)
            error = ec.message();
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.directory != b.directory ? a.directory : lower(a.name) < lower(b.name);
        });
    }
    // Opens a folder or returns a chosen file for the current selection.
    std::optional<std::string> commit() {
        fs::path chosen(selection.data());
        if (chosen.empty())
            return std::nullopt;
        if (chosen.is_relative())
            chosen = directory / chosen;
        std::error_code ec;
        if (fs::is_directory(chosen, ec)) {
            navigate(chosen);
            return std::nullopt;
        }
        if (!fs::exists(chosen, ec)) {
            error = "File not found: " + chosen.string();
            return std::nullopt;
        }
        return fs::absolute(chosen, ec).lexically_normal().string();
    }
    bool selection_is_directory() const {
        if (!selection[0])
            return false;
        fs::path chosen(selection.data());
        std::error_code ec;
        return fs::is_directory(chosen.is_relative() ? directory / chosen : chosen, ec);
    }
    std::optional<std::string> draw() {
        if (request) {
            ImGui::OpenPopup("###file-browser");
            request = false;
        }
        std::optional<std::string> result;
        ImGui::SetNextWindowSize({760, 540}, ImGuiCond_FirstUseEver);
        auto heading = title + "###file-browser";
        if (!ImGui::BeginPopupModal(heading.c_str(), nullptr, ImGuiWindowFlags_NoCollapse))
            return result;
        if (stale)
            refresh();
        std::optional<fs::path> next;
        ImGui::BeginDisabled(directory == directory.root_path());
        if (ImGui::Button("Up"))
            next = directory.parent_path();
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Parent folder");
        ImGui::SameLine(0, 4);
        if (ImGui::Button("Home"))
            next = home_directory();
        ImGui::SameLine(0, 4);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##directory", "Folder path, press Enter", location.data(),
                                     location.size(), ImGuiInputTextFlags_EnterReturnsTrue))
            next = fs::path(location.data());

        float footer = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetTextLineHeightWithSpacing() +
                       ImGui::GetStyle().ItemSpacing.y * 2;
        ImGui::BeginChild("files", {0, -footer}, ImGuiChildFlags_Borders);
        if (entries.empty())
            label("This folder is empty.");
        for (const auto& entry : entries) {
            auto text = (entry.directory ? "[DIR]  " : "       ") + entry.name;
            bool is_selected = entry.name == selection.data();
            if (entry.directory)
                ImGui::PushStyleColor(ImGuiCol_Text, accent);
            if (ImGui::Selectable(text.c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                assign(selection, entry.name);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (entry.directory)
                        next = directory / entry.name;
                    else
                        result = commit();
                }
            }
            if (entry.directory)
                ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        if (next)
            navigate(*next);

        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##filename", "File name or full path, press Enter to choose",
                                     selection.data(), selection.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
            result = commit();
        if (!error.empty())
            ImGui::TextColored(red, "%s", error.c_str());
        else
            label("Double-click a folder to open it, or a file to choose it.");
        ImGui::BeginDisabled(selection[0] == 0);
        if (primary(selection_is_directory() ? "Open folder" : "Choose file", {140, 32}))
            result = commit();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {100, 32}) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        if (result)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return result;
    }
};

struct InputField {
    const char* title;
    const char* browse_title;
    const char* hint;
    const char* name;
};
constexpr std::array<InputField, 4> fields{{
    {"01  BASELINE  /  REQUIRED", "Select baseline binary", "/path/to/old/libexample.so",
     "Baseline"},
    {"02  CANDIDATE  /  OPTIONAL", "Select candidate binary", "Leave empty to inspect baseline",
     "Candidate"},
    {"03  CONSUMER  /  OPTIONAL", "Select consumer executable", "/path/to/application",
     "Consumer"},
    {"04  SUPPRESSIONS  /  OPTIONAL", "Select suppression file", "/path/to/abitur.supp",
     "Suppression file"},
}};

struct App {
    std::array<Path, 4> paths{};
    Path& old_path = paths[0];
    Path& new_path = paths[1];
    Path& consumer_path = paths[2];
    Path& suppression_path = paths[3];
    Path output{};
    bool output_edited = false;
    std::optional<abitur::Report> report;
    std::future<abitur::Report> task;
    Browser browser;
    ImGuiTextFilter findings_filter;
    FilteredList exports, imports, types;
    int generation = 0;
    int selected = -1;
    int severity_filter = 0;
    bool running = false;
    bool autorun = false;
    std::string error, notification;
    std::chrono::steady_clock::time_point started, notified;

    App() {
        assign(output, "abi-report");
    }
    bool compare_mode() const {
        return new_path[0] != 0;
    }
    // Returns why the analysis cannot run, or an empty string when it can.
    std::string blocker() const {
        if (!old_path[0])
            return "Choose a baseline binary to begin.";
        for (size_t i = 0; i < paths.size(); ++i)
            if (paths[i][0] && !file_exists(paths[i].data()))
                return std::string(fields[i].name) + " file not found.";
        return {};
    }
    void notify(std::string text) {
        notification = std::move(text);
        notified = std::chrono::steady_clock::now();
    }
    void analyze() {
        if (running || !blocker().empty())
            return;
        abitur::Options options;
        options.old_path = old_path.data();
        if (new_path[0])
            options.new_path = new_path.data();
        if (consumer_path[0])
            options.consumers.emplace_back(consumer_path.data());
        if (suppression_path[0])
            options.suppression_file = suppression_path.data();
        if (!output_edited) {
            std::error_code ec;
            auto base = fs::absolute(options.old_path, ec).parent_path();
            assign(output, (base / "abi-report").string());
        }
        error.clear();
        notification.clear();
        report.reset();
        selected = -1;
        running = true;
        started = std::chrono::steady_clock::now();
        task = std::async(std::launch::async, [options] {
            if (options.new_path.empty())
                return abitur::analyze(options);
            return abitur::compare(options);
        });
    }
    void poll() {
        if (running && task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            running = false;
            ++generation;
            try {
                report = task.get();
                if (!report->findings.empty())
                    selected = 0;
            } catch (const std::exception& e) {
                error = e.what();
            }
        }
        if (!notification.empty() &&
            std::chrono::steady_clock::now() - notified > std::chrono::seconds(6))
            notification.clear();
    }
    void drop(const std::vector<std::string>& files) {
        if (running)
            return;
        std::vector<std::string> binaries;
        for (const auto& file : files) {
            if (fs::path(file).extension() == ".supp")
                assign(suppression_path, file);
            else
                binaries.push_back(file);
        }
        if (binaries.size() > 1) {
            assign(old_path, binaries[0]);
            assign(new_path, binaries[1]);
        } else if (!binaries.empty())
            assign(old_path[0] ? new_path : old_path, binaries[0]);
    }
    void file_input(size_t index) {
        const auto& field = fields[index];
        auto& path = paths[index];
        ImGui::PushID(static_cast<int>(index));
        label(field.title);
        float buttons = path[0] ? 76.0f : 38.0f;
        ImGui::SetNextItemWidth(-buttons);
        ImGui::InputTextWithHint("##path", field.hint, path.data(), path.size());
        if (path[0])
            ImGui::SetItemTooltip("%s", path.data());
        ImGui::SameLine(0, 4);
        if (ImGui::Button("...", {32, 0}))
            browser.open(static_cast<int>(index), field.browse_title, path.data());
        ImGui::SetItemTooltip("Browse");
        if (path[0]) {
            ImGui::SameLine(0, 4);
            if (ImGui::Button("x", {32, 0}))
                path.fill(0);
            ImGui::SetItemTooltip("Clear");
        }
        if (path[0] && !file_exists(path.data()))
            ImGui::TextColored(red, "File not found");
        ImGui::PopID();
        ImGui::Spacing();
    }
    void export_report(int format) {
        static constexpr const char* extensions[] = {".txt", ".json", ".sarif"};
        try {
            if (!output[0])
                throw std::runtime_error("Enter an output path prefix");
            std::string path = std::string(output.data()) + extensions[format];
            abitur::write_file(path, format == 0   ? abitur::text(*report)
                                     : format == 1 ? abitur::json(*report)
                                                   : abitur::sarif(*report));
            notify("Saved " + fs::absolute(path).string());
            error.clear();
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    void about_popup() {
        ImGui::SetNextWindowSize({570, 0}, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("About Abitur", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Abitur %s", abitur::version);
            wrapped("Structural ABI comparison for Linux ELF and macOS Mach-O binaries.");
            ImGui::Separator();
            wrapped("Copyright (C) 2026 Suleyman Poyraz");
            wrapped("GNU General Public License, version 3 or later (GPL-3.0-or-later).");
            wrapped("This program is free software and comes without any warranty.");
            wrapped("Third-party components retain their own licenses.");
            ImGui::TextLinkOpenURL("Project source and documentation",
                                   "https://github.com/Zaryob/abitur");
            ImGui::TextLinkOpenURL(
                "License and third-party notices",
                "https://github.com/Zaryob/abitur/blob/main/THIRD_PARTY_NOTICES.md");
            if (ImGui::Button("Close", {100, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    void sidebar() {
        label("INPUTS");
        ImGui::TextUnformatted("Release inputs");
        wrapped_muted("Add a candidate to compare two releases, or leave it empty to inspect the "
                      "baseline on its own. Files can also be dropped onto the window.");
        ImGui::Dummy({0, 8});
        ImGui::BeginDisabled(running);
        for (size_t i = 0; i < fields.size(); ++i)
            file_input(i);
        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::BeginDisabled(!old_path[0] && !new_path[0]);
        if (ImGui::Button("Swap 01 / 02", {half, 0}))
            std::swap(old_path, new_path);
        ImGui::SetItemTooltip("Swap baseline and candidate");
        ImGui::EndDisabled();
        ImGui::SameLine();
        bool any = std::any_of(paths.begin(), paths.end(), [](const Path& p) { return p[0] != 0; });
        ImGui::BeginDisabled(!any);
        if (ImGui::Button("Clear all", {half, 0}))
            for (auto& p : paths)
                p.fill(0);
        ImGui::EndDisabled();
        ImGui::Dummy({0, 10});

        label("MODE");
        ImGui::SameLine();
        ImGui::TextUnformatted(compare_mode() ? "Compare two binaries" : "Inspect a single binary");
        auto reason = blocker();
        ImGui::BeginDisabled(!reason.empty());
        const char* run_label = compare_mode() ? "Compare ABI compatibility" : "Analyze binary";
        if (primary(run_label, {-1, 44}))
            analyze();
        ImGui::SetItemTooltip("%s+Enter", mod_key);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (running) {
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
            ImGui::TextColored(accent, "Analyzing binary + DWARF ... %llds",
                               static_cast<long long>(seconds));
        } else if (!reason.empty())
            ImGui::TextColored(old_path[0] ? red : muted, "%s", reason.c_str());
        else
            label((std::string(mod_key) + "+Enter to run").c_str());

        ImGui::Dummy({0, 14});
        ImGui::Separator();
        ImGui::Dummy({0, 8});
        label("INPUT CONTRACT");
        wrapped("ELF .so, Mach-O .dylib, or static .a\nDWARF debug info (-g / .dSYM)\nMatching "
                "compiler and target");
        ImGui::Dummy({0, 8});
        label("DECISION POLICY");
        ImGui::TextColored(red, "BREAK ");
        ImGui::SameLine();
        ImGui::TextUnformatted("ABI difference detected");
        ImGui::TextColored(amber, "REVIEW");
        ImGui::SameLine();
        ImGui::TextUnformatted("Incomplete or uncertain");
        ImGui::TextColored(accent, "PASS  ");
        ImGui::SameLine();
        ImGui::TextUnformatted("No supported differences");
        ImGui::Dummy({0, 6});
        wrapped_muted(
            "Direct imports explain possible consumer impact. They do not prove runtime safety.");
        ImGui::Dummy({0, 8});
        if (ImGui::TextLink("About Abitur"))
            ImGui::OpenPopup("About Abitur");
        about_popup();
    }
    void banners() {
        if (!error.empty()) {
            ImGui::TextColored(red, running ? "Error" : "Analysis / export error");
            ImGui::SameLine();
            if (ImGui::SmallButton("Dismiss##error"))
                error.clear();
            else
                wrapped(error);
            ImGui::Separator();
        }
        if (!notification.empty()) {
            ImGui::TextColored(accent, "%s", notification.c_str());
            ImGui::Spacing();
        }
    }
    void empty_state() {
        ImGui::Dummy({0, 40});
        label(running ? "READING BINARY INTERFACES" : "READY");
        wrapped(running ? "Building the type graph and comparing exported interfaces. Results "
                          "will appear here."
                        : "1. Choose a baseline binary on the left.\n"
                          "2. Optionally add a candidate, consumer or suppression file.\n"
                          "3. Press the run button (or " +
                              std::string(mod_key) +
                              "+Enter).\n\n"
                              "Dropping two files onto this window fills baseline and candidate.");
        ImGui::Dummy({0, 24});
        for (auto [title, body] :
             {std::pair{"01 / Exported symbols",
                        "Reports removed functions, globals and changed symbol properties."},
              {"02 / Signatures & layout",
               "Compares return values and parameters through pointers, records and arrays."},
              {"03 / Consumer evidence",
               "Associates affected interfaces with an application's direct imports."}}) {
            ImGui::TextColored(accent, "%s", title);
            wrapped(body);
            ImGui::Dummy({0, 18});
        }
    }
    float export_height() const {
        const auto& style = ImGui::GetStyle();
        return ImGui::GetTextLineHeightWithSpacing() * 2 + ImGui::GetFrameHeightWithSpacing() +
               style.ItemSpacing.y;
    }
    void export_bar() {
        label("EXPORT REPORT");
        float button = 72;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(
            std::max(100.0f, ImGui::GetContentRegionAvail().x - 3 * (button + spacing)));
        if (ImGui::InputTextWithHint("##output", "Output path prefix", output.data(),
                                     output.size()))
            output_edited = true;
        static constexpr const char* names[] = {"TEXT", "JSON", "SARIF"};
        for (int i = 0; i < 3; ++i) {
            ImGui::SameLine();
            if (ImGui::Button(names[i], {button, 0}))
                export_report(i);
        }
        std::error_code ec;
        auto resolved = output[0] ? fs::absolute(output.data(), ec).string() : std::string("-");
        label(("Writes " + resolved + ".{txt,json,sarif}").c_str());
    }
    void results() {
        label(report && report->elf_analysis ? "BINARY INSPECTION" : "RELEASE ANALYSIS");
        ImGui::TextUnformatted(report ? (report->elf_analysis ? "Binary report"
                                                              : "Compatibility report")
                                      : "Structural ABI comparison");
        ImGui::Spacing();
        banners();
        if (!report) {
            empty_state();
            return;
        }
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0, 0, 0, 0});
        ImGui::BeginChild("report_body", {0, -export_height()});
        if (report->elf_analysis)
            binary_report(*report);
        else
            comparison_report(*report);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        export_bar();
    }

    void binary_report(const abitur::Report& r) {
        const auto& elf = *r.elf_analysis;
        bool is_macho = elf.osabi.find("Mach-O") != std::string::npos ||
                        elf.file_type.find("Mach-O") != std::string::npos;
        ImGui::TextColored(accent, "%s ANALYSIS  /  %s", is_macho ? "MACH-O" : "ELF BINARY",
                           elf.file_type.c_str());
        label(elf.path.c_str());
        for (const auto& warning : elf.warnings) {
            ImGui::PushStyleColor(ImGuiCol_Text, amber);
            wrapped("! " + warning);
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        if (!ImGui::BeginTabBar("elf_tabs"))
            return;
        constexpr auto table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                     ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTabItem("Overview")) {
            ImGui::BeginChild("overview", {0, 0}, ImGuiChildFlags_None);
            ImGui::Spacing();
            if (ImGui::BeginTable("headers", 2, ImGuiTableFlags_SizingFixedFit)) {
                key_value("Architecture", elf.machine);
                key_value("Class / Endian", elf.elf_class + " | " + elf.endianness);
                key_value("OS / ABI",
                          elf.osabi + " (version " + std::to_string(elf.abi_version) + ")");
                if (elf.entry_point)
                    key_value("Entry point", hex(elf.entry_point));
                if (!elf.soname.empty())
                    key_value(is_macho ? "Install name" : "SONAME", elf.soname);
                if (!elf.rpath.empty())
                    key_value("RPATH", elf.rpath);
                if (!elf.runpath.empty())
                    key_value("RUNPATH", elf.runpath);
                key_value("Exports / Imports", std::to_string(elf.exports.size()) + " / " +
                                                   std::to_string(elf.imports.size()));
                key_value("Type layouts", std::to_string(elf.type_layouts.size()));
                ImGui::EndTable();
            }
            ImGui::Dummy({0, 10});
            ImGui::Separator();
            label("SECURITY PROFILE");
            if (elf.security_features.empty())
                label("No security features reported.");
            for (const auto& feat : elf.security_features) {
                bool is_alert = feat.find("security alert") != std::string::npos;
                ImGui::TextColored(is_alert ? red : accent, "- %s", feat.c_str());
            }
            ImGui::Dummy({0, 10});
            ImGui::Separator();
            label(is_macho ? "LOADED DYLIBS (LC_LOAD_DYLIB)" : "DYNAMIC DEPENDENCIES (DT_NEEDED)");
            if (elf.needed_libraries.empty())
                label("None.");
            for (const auto& lib : elf.needed_libraries)
                ImGui::Text("  %s", lib.c_str());
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        auto exports_tab = "Exports (" + std::to_string(elf.exports.size()) + ")###exports";
        if (ImGui::BeginTabItem(exports_tab.c_str())) {
            ImGui::Spacing();
            const auto& rows = exports.update(
                static_cast<int>(elf.exports.size()), generation, [&](int i) {
                    const auto& s = elf.exports[i];
                    return s.name + " " + s.demangled + " " + s.signature;
                });
            search_box(exports.filter, "exports", "Search name or signature", 280);
            ImGui::SameLine();
            label(("Showing " + std::to_string(rows.size()) + " of " +
                   std::to_string(elf.exports.size()))
                      .c_str());
            if (ImGui::BeginTable("exports_table", 5, table_flags)) {
                ImGui::TableSetupColumn("NAME", ImGuiTableColumnFlags_WidthStretch, 0.35f);
                ImGui::TableSetupColumn("TYPE", ImGuiTableColumnFlags_WidthFixed, 65);
                ImGui::TableSetupColumn("BIND", ImGuiTableColumnFlags_WidthFixed, 65);
                ImGui::TableSetupColumn("SIZE", ImGuiTableColumnFlags_WidthFixed, 55);
                ImGui::TableSetupColumn("SIGNATURE", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(rows.size()));
                while (clipper.Step()) {
                    for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
                        const auto& sym = elf.exports[rows[k]];
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(sym.name.c_str());
                        if (!sym.demangled.empty() && sym.demangled != sym.name) {
                            ImGui::SetItemTooltip("%s", sym.demangled.c_str());
                            ImGui::SameLine();
                            ImGui::TextColored(muted, "(%s)", sym.demangled.c_str());
                        }
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(sym.type.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(sym.binding.c_str());
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%llu", static_cast<unsigned long long>(sym.size));
                        ImGui::TableSetColumnIndex(4);
                        ImGui::TextColored(accent, "%s",
                                           sym.signature.empty() ? "-" : sym.signature.c_str());
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        auto imports_tab = "Imports (" + std::to_string(elf.imports.size()) + ")###imports";
        if (ImGui::BeginTabItem(imports_tab.c_str())) {
            ImGui::Spacing();
            const auto& rows = imports.update(static_cast<int>(elf.imports.size()), generation,
                                              [&](int i) { return elf.imports[i]; });
            search_box(imports.filter, "imports", "Search imported symbols", 280);
            ImGui::SameLine();
            label(("Showing " + std::to_string(rows.size()) + " of " +
                   std::to_string(elf.imports.size()))
                      .c_str());
            if (ImGui::BeginTable("imports_table", 1, table_flags)) {
                ImGui::TableSetupColumn("SYMBOL NAME", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(rows.size()));
                while (clipper.Step()) {
                    for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(elf.imports[rows[k]].c_str());
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Sections & Segments")) {
            ImGui::Spacing();
            label(("PROGRAM HEADERS / SEGMENTS  (" + std::to_string(elf.segments.size()) + ")")
                      .c_str());
            float segment_rows = static_cast<float>(std::min<size_t>(elf.segments.size(), 8) + 1);
            if (ImGui::BeginTable("segments_table", 6, table_flags,
                                  {0, segment_rows * ImGui::GetFrameHeight() + 8})) {
                ImGui::TableSetupColumn("TYPE", ImGuiTableColumnFlags_WidthFixed, 110);
                ImGui::TableSetupColumn("VADDR", ImGuiTableColumnFlags_WidthFixed, 90);
                ImGui::TableSetupColumn("FILE SZ", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("MEM SZ", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("FLAGS", ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("ALIGN", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                for (const auto& seg : elf.segments) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(seg.type.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(hex(seg.vaddr).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(hex(seg.file_size).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(hex(seg.mem_size).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(seg.flags.c_str());
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextUnformatted(hex(seg.align).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::Dummy({0, 10});
            label(("SECTIONS  (" + std::to_string(elf.sections.size()) + ")").c_str());
            if (ImGui::BeginTable("sections_table", 5, table_flags)) {
                ImGui::TableSetupColumn("NAME", ImGuiTableColumnFlags_WidthStretch, 0.35f);
                ImGui::TableSetupColumn("TYPE", ImGuiTableColumnFlags_WidthFixed, 100);
                ImGui::TableSetupColumn("ADDRESS", ImGuiTableColumnFlags_WidthFixed, 90);
                ImGui::TableSetupColumn("SIZE", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("FLAGS", ImGuiTableColumnFlags_WidthStretch, 0.35f);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                for (const auto& sec : elf.sections) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(sec.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(sec.type.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(hex(sec.address).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(hex(sec.size).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(sec.flags.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        auto types_tab =
            "Type Layouts (" + std::to_string(elf.type_layouts.size()) + ")###types";
        if (ImGui::BeginTabItem(types_tab.c_str())) {
            ImGui::Spacing();
            const auto& rows =
                types.update(static_cast<int>(elf.type_layouts.size()), generation,
                             [&](int i) { return elf.type_layouts[i].name; });
            search_box(types.filter, "types", "Search type names", 280);
            ImGui::SameLine();
            label(("Showing " + std::to_string(rows.size()) + " of " +
                   std::to_string(elf.type_layouts.size()))
                      .c_str());
            ImGui::BeginChild("types_child", {0, 0}, ImGuiChildFlags_Borders);
            if (elf.type_layouts.empty())
                wrapped_muted("No DWARF type layouts found. Build with -g (or provide a .dSYM) to "
                              "inspect struct layouts.");
            for (int i : rows) {
                const auto& tl = elf.type_layouts[i];
                uint64_t padding = 0;
                int holes = 0;
                for (const auto& m : tl.members)
                    if (m.is_padding) {
                        padding += m.size;
                        ++holes;
                    }
                std::string header = tl.kind + " " + tl.name + "  (" +
                                     std::to_string(tl.byte_size) + " bytes, align " +
                                     std::to_string(tl.alignment) + ")";
                if (holes)
                    header += "  - " + std::to_string(padding) + " bytes padding";
                ImGui::PushID(i);
                if (holes)
                    ImGui::PushStyleColor(ImGuiCol_Text, amber);
                bool open = ImGui::CollapsingHeader(header.c_str());
                if (holes)
                    ImGui::PopStyleColor();
                if (open) {
                    for (const auto& m : tl.members) {
                        if (m.is_padding) {
                            ImGui::TextColored(amber, "    +0x%02llx  [%llu bytes padding/hole]",
                                               static_cast<unsigned long long>(m.offset),
                                               static_cast<unsigned long long>(m.size));
                        } else {
                            ImGui::Text("    +0x%02llx  %-16s %s (%llu bytes)",
                                        static_cast<unsigned long long>(m.offset), m.type.c_str(),
                                        m.name.c_str(), static_cast<unsigned long long>(m.size));
                        }
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (!r.consumers.empty() && ImGui::BeginTabItem("Consumers")) {
            ImGui::Spacing();
            for (const auto& c : r.consumers) {
                ImGui::TextColored(accent, "Consumer: %s", c.path.c_str());
                ImGui::Text("Matched imports: %zu", c.imports.size());
                ImGui::Dummy({0, 10});
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    void copy_button(const char* text, const std::string& value) {
        if (ImGui::SmallButton(text)) {
            ImGui::SetClipboardText(value.c_str());
            notify("Copied to clipboard: " + value);
        }
        ImGui::SetItemTooltip("%s", value.c_str());
    }
    void evidence(const abitur::Finding& f) {
        ImGui::TextColored(color(f.severity), "%s", f.rule.c_str());
        ImGui::SameLine();
        label(("/  " + f.severity).c_str());
        wrapped(f.message);
        ImGui::Separator();
        label("SYMBOL");
        wrapped(f.symbol.empty() ? "Library / coverage" : f.symbol);
        if (!f.rule.empty()) {
            std::string entry = f.symbol.empty() ? f.rule : f.rule + ":" + f.symbol;
            label("SUPPRESS");
            copy_button("Copy CLI flag", "--suppress " + entry);
            ImGui::SameLine();
            copy_button("Copy suppression-file line", entry);
        }
        ImGui::Spacing();
        label("STRUCTURAL EVIDENCE");
        if (f.evidence.empty())
            wrapped_muted("No additional structural evidence for this finding.");
        for (const auto& e : f.evidence)
            wrapped(e);
        for (const auto& c : f.consumers) {
            ImGui::Spacing();
            ImGui::TextColored(amber, "Direct consumer import");
            wrapped(c);
        }
    }
    void comparison_report(const abitur::Report& r) {
        ImVec4 decision_color = r.decision == "break"    ? red
                                : r.decision == "review" ? amber
                                                         : accent;
        ImGui::TextColored(decision_color, "%s  /  CI EXIT %d",
                           r.decision == "break"    ? "BREAK"
                           : r.decision == "review" ? "REVIEW"
                                                    : "PASS",
                           r.exit_code());
        wrapped(r.reason);
        std::array<int, 3> counts{};
        for (const auto& f : r.findings)
            ++counts[f.severity == "error" ? 0 : f.severity == "warning" ? 1 : 2];
        auto plural = [](int n) { return n == 1 ? "" : "s"; };
        ImGui::TextColored(counts[0] ? red : muted, "%d error%s", counts[0], plural(counts[0]));
        ImGui::SameLine(0, 16);
        ImGui::TextColored(counts[1] ? amber : muted, "%d warning%s", counts[1], plural(counts[1]));
        ImGui::SameLine(0, 16);
        ImGui::TextColored(counts[2] ? accent : muted, "%d note%s", counts[2], plural(counts[2]));
        ImGui::SameLine(0, 16);
        label(("|  " + r.scope).c_str());
        if (ImGui::TreeNode("Compared artifacts")) {
            wrapped("Baseline:  " + r.old_path);
            wrapped("Candidate: " + r.new_path);
            ImGui::TreePop();
        }
        ImGui::Spacing();

        static constexpr const char* severities[] = {"", "error", "warning", "note"};
        std::vector<int> rows;
        for (int i = 0; i < static_cast<int>(r.findings.size()); ++i) {
            const auto& f = r.findings[i];
            auto searchable = f.symbol + " " + f.message + " " + f.rule;
            if (severity_filter && f.severity != severities[severity_filter])
                continue;
            if (findings_filter.PassFilter(searchable.c_str()))
                rows.push_back(i);
        }
        search_box(findings_filter, "findings", "Search symbol, rule or message", 280);
        ImGui::SameLine();
        std::string items = "All (" + std::to_string(r.findings.size()) + ")";
        items += '\0';
        static constexpr const char* names[] = {"Errors", "Warnings", "Notes"};
        for (int i = 0; i < 3; ++i) {
            items += std::string(names[i]) + " (" + std::to_string(counts[i]) + ")";
            items += '\0';
        }
        ImGui::SetNextItemWidth(170);
        ImGui::Combo("##severity", &severity_filter, items.c_str());
        ImGui::SameLine();
        label(("Showing " + std::to_string(rows.size()) + " of " +
               std::to_string(r.findings.size()))
                  .c_str());

        // Fit the table to its rows (at least 5, at most ~45% of the space) so the
        // evidence panel gets the remaining room.
        float row_height = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2;
        float wanted = (static_cast<float>(std::max<size_t>(rows.size(), 5)) + 1.5f) * row_height;
        float table_height =
            std::clamp(wanted, 130.0f, std::max(130.0f, ImGui::GetContentRegionAvail().y * 0.45f));
        if (ImGui::BeginTable("findings", 4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                              {0, table_height})) {
            ImGui::TableSetupColumn("SEVERITY", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("RULE", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("SYMBOL", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableSetupColumn("CHANGE", ImGuiTableColumnFlags_WidthStretch, 0.62f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (int i : rows) {
                const auto& f = r.findings[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_Text, color(f.severity));
                if (ImGui::Selectable(f.severity.c_str(), selected == i,
                                      ImGuiSelectableFlags_SpanAllColumns))
                    selected = i;
                // Keyboard navigation (arrow keys) follows the focused row.
                if (ImGui::IsItemFocused() && ImGui::IsItemVisible() && selected != i &&
                    !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    selected = i;
                ImGui::PopStyleColor();
                ImGui::TableSetColumnIndex(1);
                label(f.rule.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(f.symbol.empty() ? "Library / coverage" : f.symbol.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(f.message.c_str());
                ImGui::PopID();
            }
            if (rows.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(2);
                label(r.findings.empty() ? "No findings. Nothing to review."
                                         : "No findings match the current filter.");
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::BeginChild("detail", {0, 0}, ImGuiChildFlags_Borders);
        if (ImGui::BeginTabBar("details")) {
            if (ImGui::BeginTabItem("Evidence")) {
                if (selected >= 0 && selected < static_cast<int>(r.findings.size()))
                    evidence(r.findings[selected]);
                else
                    wrapped_muted("Select a finding to inspect its evidence. Use the arrow keys "
                                  "to move through the list.");
                ImGui::EndTabItem();
            }
            auto consumers_tab =
                "Consumers (" + std::to_string(r.consumers.size()) + ")###consumers";
            if (ImGui::BeginTabItem(consumers_tab.c_str())) {
                if (r.consumers.empty())
                    wrapped_muted("No consumer supplied. Add an application or library in "
                                  "03 CONSUMER to inspect direct imports.");
                for (const auto& c : r.consumers) {
                    wrapped(c.path);
                    ImGui::TextColored(c.affected.empty() ? muted : amber,
                                       "%zu matched imports / %zu affected", c.imports.size(),
                                       c.affected.size());
                    for (const auto& s : c.affected)
                        wrapped(s);
                    if (c.affected.empty())
                        wrapped_muted("No affected direct import observed. Dynamic lookup and "
                                      "indirect use remain unknown.");
                    ImGui::Spacing();
                }
                ImGui::EndTabItem();
            }
            auto limits_tab =
                "Coverage & limits (" + std::to_string(r.limitations.size()) + ")###limits";
            if (ImGui::BeginTabItem(limits_tab.c_str())) {
                if (r.limitations.empty())
                    wrapped_muted("No coverage limitations reported.");
                for (const auto& l : r.limitations) {
                    wrapped(l);
                    ImGui::Spacing();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }
    std::string activity() const {
        if (running)
            return compare_mode() ? "Comparing binaries..." : "Analyzing binary...";
        if (!error.empty() && !report)
            return "Analysis failed";
        if (!report)
            return "Ready";
        if (report->elf_analysis)
            return "Inspection complete / " + fs::path(report->elf_analysis->path).filename().string();
        return "Comparison complete / " + report->decision + " / " +
               std::to_string(report->findings.size()) + " findings";
    }
    void draw(abitur::desktop::Rect area) {
        poll();
        if (autorun) {
            autorun = false;
            analyze();
        }
        bool modal =
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (!modal && (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Enter) ||
                       ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadEnter)))
            analyze();
        ImGui::SetNextWindowPos({static_cast<float>(area.x), static_cast<float>(area.y)});
        ImGui::SetNextWindowSize({static_cast<float>(area.width), static_cast<float>(area.height)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::Begin("Abitur", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        float sidebar_width = std::clamp(ImGui::GetContentRegionAvail().x * 0.29f, 300.0f, 380.0f);
        ImGui::BeginChild("sidebar", {sidebar_width, 0}, ImGuiChildFlags_Borders);
        sidebar();
        ImGui::EndChild();
        ImGui::SameLine(0, 20);
        ImGui::BeginChild("results", {0, 0});
        results();
        ImGui::EndChild();
        if (auto chosen = browser.draw())
            assign(paths[static_cast<size_t>(browser.destination)], *chosen);
        ImGui::End();
        ImGui::PopStyleVar();
    }
};
} // namespace

int main(int argc, char** argv) {
    try {
        abitur::desktop::Window window;
        App app;
        int frame_limit = 0;
        std::vector<std::string> paths;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--smoke-test")
                frame_limit = 30;
            else
                paths.emplace_back(argv[i]);
        }
        if (!paths.empty())
            assign(app.old_path, paths[0]);
        if (paths.size() > 1) {
            assign(app.new_path, paths[1]);
            app.autorun = true;
        }
        if (paths.size() > 2)
            assign(app.consumer_path, paths[2]);
        window.set_drop_handler([&app](const std::vector<std::string>& files) { app.drop(files); });
        int frames = 0;
        while (!window.should_close()) {
            if (!window.begin_frame()) {
                app.poll();
                continue;
            }
            abitur::desktop::ChromeState state;
            state.busy = app.running;
            state.activity = app.activity();
            state.detail = std::string("v") + abitur::version;
            app.draw(window.draw_chrome(state));
            window.end_frame();
            if (frame_limit && ++frames >= frame_limit && !app.running)
                break;
        }
        // App joins its future before the shared desktop window releases ImGui.
        if (frame_limit && !app.error.empty()) {
            std::cerr << app.error << '\n';
            return 2;
        }
        if (frame_limit) {
            if (paths.size() > 1 && !app.report) {
                std::cerr << "No analysis report produced\n";
                return 2;
            }
            std::cout << "ImGui smoke test: " << frames << " frames";
            if (app.report)
                std::cout << ", decision=" << app.report->decision
                          << ", findings=" << app.report->findings.size();
            std::cout << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Abitur desktop: " << e.what() << '\n';
        return 2;
    }
}
