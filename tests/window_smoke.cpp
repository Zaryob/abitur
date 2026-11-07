// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "abitur/desktop/window.hpp"
#include "imgui.h"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        abitur::desktop::Window window;
        if (window.decorated())
            throw std::runtime_error("Native decorations must be disabled");
        auto original = window.bounds();
        int frames = 0;
        // Exercise the same shared commands used by title-bar buttons. Maximize
        // is implemented in work-area coordinates, so Xvfb needs no WM.
        while (!window.should_close() && frames < 30) {
            if (!window.begin_frame())
                continue;
            auto area =
                window.draw_chrome({"Desktop window smoke test", "Shared ImGui chrome", false});
            ImGui::SetNextWindowPos({static_cast<float>(area.x), static_cast<float>(area.y)});
            ImGui::SetNextWindowSize(
                {static_cast<float>(area.width), static_cast<float>(area.height)});
            ImGui::Begin("##test-content", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
            ImGui::TextUnformatted("Shared desktop window: Windows / macOS / Linux");
            ImGui::TextUnformatted("Title bar, resize edges, controls, theme and embedded font.");
            ImGui::End();
            window.end_frame();
            ++frames;
            if (frames == 5)
                window.toggle_maximize();
            if (frames == 10 && !window.maximized())
                throw std::runtime_error("Maximize state was lost");
            if (frames == 15)
                window.restore();
            if (frames == 20) {
                if (window.maximized())
                    throw std::runtime_error("Restore did not clear maximize state");
                auto restored = window.bounds();
                if (restored != original)
                    throw std::runtime_error("Restore did not preserve window position and size");
            }
        }
        window.close();
        if (!window.should_close())
            throw std::runtime_error("Close did not set window state");
        std::cout
            << "Shared ImGui window: 30 frames; borderless, maximize, restore and close passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
