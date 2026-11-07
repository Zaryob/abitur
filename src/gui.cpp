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
        if (running || !old_path[0] || !new_path[0])
            return;
        abitur::Options options{old_path.data(), new_path.data(), {}};
        if (consumer_path[0])
            options.consumers.emplace_back(consumer_path.data());
        error.clear();
        notification.clear();
        report.reset();
        selected = -1;
        running = true;
        started = std::chrono::steady_clock::now();
        task = std::async(std::launch::async, [options] { return abitur::compare(options); });
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
        file_input("01  BASELINE", "/path/to/old/libexample.so", old_path, 0);
        file_input("02  CANDIDATE", "/path/to/new/libexample.so", new_path, 1);
        file_input("03  CONSUMER  /  OPTIONAL", "/path/to/application", consumer_path, 2);
        if (ImGui::SmallButton("Swap baseline / candidate"))
            std::swap(old_path, new_path);
        ImGui::Dummy({0, 12});
        ImGui::BeginDisabled(!old_path[0] || !new_path[0]);
        ImGui::PushStyleColor(ImGuiCol_Button, {0.17f, 0.42f, 0.35f, 1});
        if (ImGui::Button("Analyze compatibility", {-1, 44}))
            analyze();
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (running) {
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
            ImGui::TextColored(accent, "Analyzing ELF + DWARF ... %llds",
                               static_cast<long long>(seconds));
        }
        ImGui::Dummy({0, 20});
        ImGui::Separator();
        ImGui::Dummy({0, 12});
        label("INPUT CONTRACT");
        wrapped("Linux ELF .so or regular .a\nEmbedded DWARF (-g)\nMatching compiler and target");
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
                        "No consumer supplied. Add an ELF application to inspect direct imports.");
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
