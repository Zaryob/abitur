// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "abitur/analyzer.hpp"
#include "abitur/desktop/window.hpp"
#include "abitur/version.hpp"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr ImVec4 accent{0.42f, 0.84f, 0.71f, 1};
constexpr ImVec4 muted{0.50f, 0.56f, 0.62f, 1};
constexpr ImVec4 red{1.0f, 0.43f, 0.41f, 1};
constexpr ImVec4 amber{0.96f, 0.73f, 0.38f, 1};
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

struct Browser {
    bool request = false;
    int destination = 0;
    fs::path directory = fs::current_path();
    std::array<char, 4096> location{};
    std::array<char, 4096> selection{};
    std::string error;
    void open(int target, const char* current) {
        destination = target;
        request = true;
        std::error_code ec;
        fs::path path(current);
        if (!path.empty() && fs::exists(path, ec))
            directory = fs::is_directory(path, ec) ? path : fs::absolute(path, ec).parent_path();
        assign(location, directory.string());
        selection.fill(0);
        error.clear();
    }
    std::optional<std::string> draw() {
        if (request) {
            ImGui::OpenPopup("Choose ELF or archive");
            request = false;
        }
        std::optional<std::string> result;
        ImGui::SetNextWindowSize({720, 510}, ImGuiCond_FirstUseEver);
        if (ImGui::BeginPopupModal("Choose ELF or archive", nullptr, ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::Button("Up")) {
                directory = directory.parent_path();
                assign(location, directory.string());
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##directory", location.data(), location.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                std::error_code ec;
                if (fs::is_directory(location.data(), ec))
                    directory = location.data();
                else
                    error = "Directory does not exist";
            }
            ImGui::BeginChild("files", {0, -112}, ImGuiChildFlags_Borders);
            std::error_code ec;
            std::vector<fs::directory_entry> entries;
            fs::directory_iterator iterator(directory,
                                            fs::directory_options::skip_permission_denied, ec),
                end;
            while (!ec && iterator != end) {
                entries.push_back(*iterator);
                iterator.increment(ec);
            }
            if (ec)
                error = ec.message();
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                std::error_code ea, eb;
                bool da = a.is_directory(ea), db = b.is_directory(eb);
                return da != db ? da > db : a.path().filename() < b.path().filename();
            });
            std::optional<fs::path> next;
            for (const auto& entry : entries) {
                auto filename = entry.path().filename().string();
                if (filename.starts_with('.'))
                    continue;
                std::error_code type_error;
                bool dir = entry.is_directory(type_error);
                auto title = (dir ? "[DIR]  " : "       ") + filename;
                if (ImGui::Selectable(title.c_str(), filename == selection.data(),
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (dir && ImGui::IsMouseDoubleClicked(0))
                        next = entry.path();
                    else if (!dir) {
                        assign(selection, filename);
                        if (ImGui::IsMouseDoubleClicked(0))
                            result = fs::absolute(entry.path()).string();
                    }
                }
            }
            if (next) {
                directory = *next;
                assign(location, directory.string());
                selection.fill(0);
            }
            ImGui::EndChild();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##filename", "Filename", selection.data(), selection.size());
            if (!error.empty())
                ImGui::TextColored(red, "%s", error.c_str());
            ImGui::BeginDisabled(selection[0] == 0);
            if (ImGui::Button("Choose file", {130, 32}))
                result = fs::absolute(directory / selection.data()).string();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {100, 32}))
                ImGui::CloseCurrentPopup();
            if (result)
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return result;
    }
};

struct App {
    std::array<char, 4096> old_path{}, new_path{}, consumer_path{};
    std::array<char, 4096> output{};
    std::optional<abitur::Report> report;
    std::future<abitur::Report> task;
    Browser browser;
    ImGuiTextFilter filter;
    int selected = -1;
    int severity_filter = 0;
    bool running = false;
    bool autorun = false;
    std::string error, notification;
    std::chrono::steady_clock::time_point started;
    App() {
        assign(output, "abi-report");
    }
    void analyze() {
        if (running || !old_path[0])
            return;
        abitur::Options options;
        options.old_path = old_path.data();
        if (new_path[0])
            options.new_path = new_path.data();
        if (consumer_path[0])
            options.consumers.emplace_back(consumer_path.data());
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
            try {
                report = task.get();
                if (!report->findings.empty())
                    selected = 0;
            } catch (const std::exception& e) {
                error = e.what();
            }
        }
    }
    void file_input(const char* title, const char* hint, std::array<char, 4096>& path, int target) {
        label(title);
        ImGui::PushID(target);
        ImGui::SetNextItemWidth(-42);
        ImGui::InputTextWithHint("##path", hint, path.data(), path.size());
        ImGui::SameLine();
        if (ImGui::Button("...", {32, 0}))
            browser.open(target, path.data());
        ImGui::PopID();
        ImGui::Spacing();
    }
    void export_report(bool sarif) {
        try {
            if (!output[0])
                throw std::runtime_error("Enter an output path prefix");
            std::string path = std::string(output.data()) + (sarif ? ".sarif" : ".json");
            abitur::write_file(path, sarif ? abitur::sarif(*report) : abitur::json(*report));
            notification = "Saved " + fs::absolute(path).string();
            error.clear();
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    void sidebar() {
        label("COMPARISON");
        ImGui::TextUnformatted("Release inputs");
        if (ImGui::SmallButton("About Abitur"))
            ImGui::OpenPopup("About Abitur");
        ImGui::SetNextWindowSize({570, 0}, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("About Abitur", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Abitur %s", abitur::version);
            wrapped("Structural ABI comparison for Linux ELF libraries.");
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
            if (ImGui::Button("Close", {100, 0}))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        wrapped("Compare exported interfaces and reachable type layouts.");
        ImGui::Dummy({0, 14});
        ImGui::BeginDisabled(running);
        file_input("01  TARGET (ELF / MACH-O / .SO / .DYLIB / .A)", "/path/to/target/libexample.so", old_path, 0);
        file_input("02  CANDIDATE  /  OPTIONAL", "/path/to/candidate/libexample.so", new_path, 1);
        file_input("03  CONSUMER  /  OPTIONAL", "/path/to/application", consumer_path, 2);
        if (ImGui::SmallButton("Swap target / candidate"))
            std::swap(old_path, new_path);
        ImGui::Dummy({0, 12});
        bool has_target = old_path[0] != 0;
        bool has_candidate = new_path[0] != 0;
        ImGui::BeginDisabled(!has_target);
        ImGui::PushStyleColor(ImGuiCol_Button, {0.17f, 0.42f, 0.35f, 1});
        const char* btn_label = has_candidate ? "Compare ABI compatibility" : "Analyze binary";
        if (ImGui::Button(btn_label, {-1, 44}))
            analyze();
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (running) {
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
            ImGui::TextColored(accent, "Analyzing binary + DWARF ... %llds",
                               static_cast<long long>(seconds));
        }
        ImGui::Dummy({0, 20});
        ImGui::Separator();
        ImGui::Dummy({0, 12});
        label("INPUT CONTRACT");
        wrapped("ELF .so, Mach-O .dylib, or static .a\nDWARF debug info (-g / .dSYM)\nMatching compiler and target");
        ImGui::Dummy({0, 12});
        label("DECISION POLICY");
        ImGui::TextColored(red, "BREAK");
        ImGui::SameLine();
        ImGui::TextUnformatted("ABI difference detected");
        ImGui::TextColored(amber, "REVIEW");
        ImGui::SameLine();
        ImGui::TextUnformatted("Incomplete or uncertain");
        ImGui::TextColored(accent, "PASS");
        ImGui::SameLine();
        ImGui::TextUnformatted("No supported differences");
        ImGui::Dummy({0, 12});
        wrapped(
            "Direct imports explain possible consumer impact. They do not prove runtime safety.");
    }
    void results() {
        label("RELEASE ANALYSIS");
        ImGui::TextUnformatted(report ? "Compatibility report" : "Structural ABI comparison");
        ImGui::Spacing();
        if (!error.empty()) {
            ImGui::TextColored(red, "Analysis / export error");
            wrapped(error);
            ImGui::Separator();
        }
        if (!notification.empty()) {
            ImGui::TextColored(accent, "Report exported");
            wrapped(notification);
        }
        if (!report) {
            ImGui::Dummy({0, 50});
            label(running ? "READING BINARY INTERFACES" : "READY TO COMPARE");
            wrapped(running ? "Building the type graph and comparing exported interfaces. Results "
                              "will appear here."
                            : "Choose a baseline and candidate on the left, then run the analysis. "
                              "You can also drop two files into this window.");
            ImGui::Dummy({0, 24});
            for (auto [title, body] :
                 {std::pair{"01 / Exported symbols",
                            "Reports removed functions, globals and changed symbol properties."},
                  {"02 / Signatures & layout",
                   "Compares return values and parameters through pointers, records and arrays."},
                  {"03 / Consumer evidence",
                   "Associates affected interfaces with an application's direct ELF imports."}}) {
                ImGui::TextColored(accent, "%s", title);
                wrapped(body);
                ImGui::Dummy({0, 18});
            }
            return;
        }
        auto& r = *report;
        if (r.elf_analysis.has_value()) {
            const auto& elf = *r.elf_analysis;
            bool is_macho = elf.osabi.find("Mach-O") != std::string::npos || elf.file_type.find("Mach-O") != std::string::npos;
            ImGui::TextColored(accent, "%s ANALYSIS  /  %s", is_macho ? "MACH-O" : "ELF BINARY", elf.file_type.c_str());
            label(elf.path.c_str());
            ImGui::Spacing();

            if (ImGui::BeginTabBar("elf_tabs")) {
                if (ImGui::BeginTabItem("Overview & Headers")) {
                    ImGui::Spacing();
                    ImGui::Text("Architecture:    %s", elf.machine.c_str());
                    ImGui::Text("Class & Endian:  %s | %s", elf.elf_class.c_str(), elf.endianness.c_str());
                    ImGui::Text("OS / ABI:        %s (version %d)", elf.osabi.c_str(), elf.abi_version);
                    if (elf.entry_point)
                        ImGui::Text("Entry Point:     0x%llx", static_cast<unsigned long long>(elf.entry_point));
                    if (!elf.soname.empty())
                        ImGui::Text("%s:          %s", is_macho ? "Install Name" : "SONAME", elf.soname.c_str());
                    if (!elf.rpath.empty())
                        ImGui::Text("RPATH:           %s", elf.rpath.c_str());
                    if (!elf.runpath.empty())
                        ImGui::Text("RUNPATH:         %s", elf.runpath.c_str());

                    ImGui::Dummy({0, 10});
                    ImGui::Separator();
                    label("SECURITY PROFILE");
                    for (const auto& feat : elf.security_features) {
                        bool is_alert = feat.find("security alert") != std::string::npos;
                        ImGui::TextColored(is_alert ? red : accent, "• %s", feat.c_str());
                    }

                    if (!elf.needed_libraries.empty()) {
                        ImGui::Dummy({0, 10});
                        ImGui::Separator();
                        label(is_macho ? "LOADED DYLIBS (LC_LOAD_DYLIB)" : "DYNAMIC DEPENDENCIES (DT_NEEDED)");
                        for (const auto& lib : elf.needed_libraries) {
                            ImGui::Text("  lib: %s", lib.c_str());
                        }
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Exported Symbols")) {
                    ImGui::Spacing();
                    filter.Draw("Filter exports", 200);
                    ImGui::Spacing();
                    float table_h = ImGui::GetContentRegionAvail().y - 10;
                    if (ImGui::BeginTable("exports_table", 5,
                                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                                          {0, table_h})) {
                        ImGui::TableSetupColumn("NAME", ImGuiTableColumnFlags_WidthStretch, 0.35f);
                        ImGui::TableSetupColumn("TYPE", ImGuiTableColumnFlags_WidthFixed, 65);
                        ImGui::TableSetupColumn("BIND", ImGuiTableColumnFlags_WidthFixed, 65);
                        ImGui::TableSetupColumn("SIZE", ImGuiTableColumnFlags_WidthFixed, 55);
                        ImGui::TableSetupColumn("SIGNATURE", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                        ImGui::TableSetupScrollFreeze(0, 1);
                        ImGui::TableHeadersRow();

                        for (int i = 0; i < static_cast<int>(elf.exports.size()); ++i) {
                            const auto& sym = elf.exports[i];
                            auto search_str = sym.name + " " + sym.demangled + " " + sym.signature;
                            if (!filter.PassFilter(search_str.c_str()))
                                continue;
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(sym.name.c_str());
                            if (!sym.demangled.empty() && sym.demangled != sym.name) {
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
                            ImGui::TextColored(accent, "%s", sym.signature.empty() ? "-" : sym.signature.c_str());
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Imported Symbols")) {
                    ImGui::Spacing();
                    filter.Draw("Filter imports", 200);
                    ImGui::Spacing();
                    float table_h = ImGui::GetContentRegionAvail().y - 10;
                    if (ImGui::BeginTable("imports_table", 1,
                                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                          ImGuiTableFlags_ScrollY,
                                          {0, table_h})) {
                        ImGui::TableSetupColumn("SYMBOL NAME", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupScrollFreeze(0, 1);
                        ImGui::TableHeadersRow();
                        for (const auto& imp : elf.imports) {
                            if (!filter.PassFilter(imp.c_str()))
                                continue;
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(imp.c_str());
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Sections & Segments")) {
                    ImGui::Spacing();
                    label("PROGRAM HEADERS / SEGMENTS");
                    if (ImGui::BeginTable("segments_table", 6,
                                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                          ImGuiTableFlags_Resizable,
                                          {0, 140})) {
                        ImGui::TableSetupColumn("TYPE", ImGuiTableColumnFlags_WidthFixed, 110);
                        ImGui::TableSetupColumn("VADDR", ImGuiTableColumnFlags_WidthFixed, 90);
                        ImGui::TableSetupColumn("FILE SZ", ImGuiTableColumnFlags_WidthFixed, 80);
                        ImGui::TableSetupColumn("MEM SZ", ImGuiTableColumnFlags_WidthFixed, 80);
                        ImGui::TableSetupColumn("FLAGS", ImGuiTableColumnFlags_WidthFixed, 60);
                        ImGui::TableSetupColumn("ALIGN", ImGuiTableColumnFlags_WidthFixed, 70);
                        ImGui::TableHeadersRow();
                        for (const auto& seg : elf.segments) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(seg.type.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("0x%llx", static_cast<unsigned long long>(seg.vaddr));
                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("0x%llx", static_cast<unsigned long long>(seg.file_size));
                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("0x%llx", static_cast<unsigned long long>(seg.mem_size));
                            ImGui::TableSetColumnIndex(4);
                            ImGui::TextUnformatted(seg.flags.c_str());
                            ImGui::TableSetColumnIndex(5);
                            ImGui::Text("0x%llx", static_cast<unsigned long long>(seg.align));
                        }
                        ImGui::EndTable();
                    }

                    ImGui::Dummy({0, 10});
                    label("SECTIONS");
                    float sec_h = ImGui::GetContentRegionAvail().y - 10;
                    if (ImGui::BeginTable("sections_table", 5,
                                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                                          {0, sec_h})) {
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
                            ImGui::Text("0x%llx", static_cast<unsigned long long>(sec.address));
                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("0x%llx", static_cast<unsigned long long>(sec.size));
                            ImGui::TableSetColumnIndex(4);
                            ImGui::TextUnformatted(sec.flags.c_str());
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Type & Struct Layouts")) {
                    ImGui::Spacing();
                    filter.Draw("Filter types", 200);
                    ImGui::Spacing();
                    ImGui::BeginChild("types_child", {0, 0}, ImGuiChildFlags_Borders);
                    for (int i = 0; i < static_cast<int>(elf.type_layouts.size()); ++i) {
                        const auto& tl = elf.type_layouts[i];
                        if (!filter.PassFilter(tl.name.c_str()))
                            continue;
                        std::string header = tl.kind + " " + tl.name + " (" +
                                             std::to_string(tl.byte_size) + " bytes, align " +
                                             std::to_string(tl.alignment) + ")";
                        if (ImGui::CollapsingHeader(header.c_str())) {
                            for (const auto& m : tl.members) {
                                if (m.is_padding) {
                                    ImGui::TextColored(amber, "    +0x%02llx  [%llu bytes padding/hole]",
                                                       static_cast<unsigned long long>(m.offset),
                                                       static_cast<unsigned long long>(m.size));
                                } else {
                                    ImGui::Text("    +0x%02llx  %-16s %s (%llu bytes)",
                                                static_cast<unsigned long long>(m.offset),
                                                m.type.c_str(), m.name.c_str(),
                                                static_cast<unsigned long long>(m.size));
                                }
                            }
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (!r.consumers.empty() && ImGui::BeginTabItem("Consumer Verification")) {
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
            return;
        }

        ImVec4 decision_color = r.decision == "break"    ? red
                                : r.decision == "review" ? amber
                                                         : accent;
        ImGui::TextColored(decision_color, "%s  /  CI EXIT %d",
                           r.decision == "break"    ? "BREAK"
                           : r.decision == "review" ? "REVIEW"
                                                    : "PASS",
                           r.exit_code());
        wrapped(r.reason);
        label((r.scope + "  |  " + std::to_string(r.findings.size()) + " findings").c_str());
        if (ImGui::TreeNode("Compared artifacts")) {
            wrapped("Baseline: " + r.old_path);
            wrapped("Candidate: " + r.new_path);
            ImGui::TreePop();
        }
        ImGui::Spacing();
        ImGui::SetNextItemWidth(180);
        filter.Draw("Filter symbols / findings", 180);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(145);
        ImGui::Combo("##severity", &severity_filter, "All findings\0Errors\0Warnings\0Notes\0");
        ImGui::Spacing();
        float table_height = std::max(130.0f, ImGui::GetContentRegionAvail().y * 0.39f);
        if (ImGui::BeginTable("findings", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                              {0, table_height})) {
            ImGui::TableSetupColumn("SEVERITY", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("SYMBOL", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableSetupColumn("CHANGE", ImGuiTableColumnFlags_WidthStretch, 0.62f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (int i = 0; i < static_cast<int>(r.findings.size()); ++i) {
                const auto& f = r.findings[i];
                auto searchable = f.symbol + " " + f.message + " " + f.rule;
                if (!filter.PassFilter(searchable.c_str()))
                    continue;
                if (severity_filter && f.severity != (severity_filter == 1   ? "error"
                                                      : severity_filter == 2 ? "warning"
                                                                             : "note"))
                    continue;
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_Text, color(f.severity));
                if (ImGui::Selectable(f.severity.c_str(), selected == i,
                                      ImGuiSelectableFlags_SpanAllColumns))
                    selected = i;
                ImGui::PopStyleColor();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(f.symbol.empty() ? "Library / coverage" : f.symbol.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(f.message.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::BeginChild("detail", {0, -80}, ImGuiChildFlags_Borders);
        if (ImGui::BeginTabBar("details")) {
            if (ImGui::BeginTabItem("Evidence")) {
                if (selected >= 0 && selected < static_cast<int>(r.findings.size())) {
                    const auto& f = r.findings[selected];
                    ImGui::TextColored(color(f.severity), "%s", f.rule.c_str());
                    wrapped(f.message);
                    if (!f.symbol.empty()) {
                        ImGui::Separator();
                        wrapped(f.symbol);
                    }
                    for (const auto& e : f.evidence) {
                        ImGui::Spacing();
                        wrapped(e);
                    }
                    if (f.evidence.empty())
                        label("No additional structural evidence for this finding.");
                    for (const auto& c : f.consumers) {
                        ImGui::Spacing();
                        ImGui::TextColored(amber, "Direct consumer import");
                        wrapped(c);
                    }
                } else
                    wrapped("Select a finding to inspect its evidence.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Consumers")) {
                if (r.consumers.empty())
                    wrapped(
                        "No consumer supplied. Add an application or library to inspect direct imports.");
                for (const auto& c : r.consumers) {
                    wrapped(c.path);
                    ImGui::TextColored(c.affected.empty() ? muted : amber,
                                       "%zu matched imports / %zu affected", c.imports.size(),
                                       c.affected.size());
                    for (const auto& s : c.affected)
                        wrapped(s);
                    if (c.affected.empty())
                        wrapped("No affected direct import observed. Dynamic lookup and indirect "
                                "use remain unknown.");
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Coverage & limits")) {
                for (const auto& l : r.limitations) {
                    wrapped(l);
                    ImGui::Spacing();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        ImGui::Spacing();
        label("EXPORT PATH PREFIX");
        ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - 185));
        ImGui::InputText("##output", output.data(), output.size());
        ImGui::SameLine();
        if (ImGui::Button("JSON", {80, 0}))
            export_report(false);
        ImGui::SameLine();
        if (ImGui::Button("SARIF", {80, 0}))
            export_report(true);
    }
    void draw(abitur::desktop::Rect area) {
        poll();
        if (autorun) {
            autorun = false;
            analyze();
        }
        ImGui::SetNextWindowPos({static_cast<float>(area.x), static_cast<float>(area.y)});
        ImGui::SetNextWindowSize({static_cast<float>(area.width), static_cast<float>(area.height)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::Begin("Abitur", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        float sidebar_width = std::clamp(ImGui::GetContentRegionAvail().x * 0.29f, 290.0f, 370.0f);
        ImGui::BeginChild("sidebar", {sidebar_width, 0}, ImGuiChildFlags_Borders);
        sidebar();
        ImGui::EndChild();
        ImGui::SameLine(0, 20);
        ImGui::BeginChild("results", {0, 0});
        results();
        ImGui::EndChild();
        if (auto chosen = browser.draw()) {
            if (browser.destination == 0)
                assign(old_path, *chosen);
            else if (browser.destination == 1)
                assign(new_path, *chosen);
            else
                assign(consumer_path, *chosen);
        }
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
        window.set_drop_handler([&app](const std::vector<std::string>& files) {
            if (app.running)
                return;
            if (files.size() > 1) {
                assign(app.old_path, files[0]);
                assign(app.new_path, files[1]);
            } else if (!files.empty())
                assign(app.old_path[0] ? app.new_path : app.old_path, files[0]);
        });
        int frames = 0;
        while (!window.should_close()) {
            if (!window.begin_frame()) {
                app.poll();
                continue;
            }
            abitur::desktop::ChromeState state;
            state.busy = app.running;
            state.activity = app.running  ? "Analyzing compatibility..."
                             : app.report ? "Analysis complete / " + app.report->decision
                                          : "Ready to compare";
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
