// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "abitur/desktop/geometry.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace abitur::desktop;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        require(hit_test({300, 200}, 1000, 700) == Edge::none, "content must not resize");
        require(hit_test({-1, 10}, 1000, 700) == Edge::none, "outside window");
        require(hit_test({2, 2}, 1000, 700) == (Edge::left | Edge::top), "top-left corner");
        require(hit_test({999, 699}, 1000, 700) == (Edge::right | Edge::bottom),
                "bottom-right corner");
        require(hit_test({500, 699}, 1000, 700) == Edge::bottom, "bottom edge");
        Rect start{-1200, 100, 1100, 800};
        require(resize(start, Edge::left | Edge::top, 700, 600, 960, 640) ==
                    Rect{-1060, 260, 960, 640},
                "left/top clamps must preserve opposite corner");
        require(resize(start, Edge::right | Edge::bottom, -700, -600, 960, 640) ==
                    Rect{-1200, 100, 960, 640},
                "right/bottom clamps must preserve origin");
        require(resize(start, Edge::left, -100, 0, 960, 640) == Rect{-1300, 100, 1200, 800},
                "negative monitor coordinates");
        require(resize(start, Edge::right | Edge::bottom, 200, 100, 960, 640) ==
                    Rect{-1200, 100, 1300, 900},
                "diagonal growth");
        const std::array monitors{Rect{-1920, 0, 1920, 1080}, Rect{0, 0, 2560, 1440}};
        require(monitor_for(start, monitors) == 0, "maximize on overlapping monitor");
        require(monitor_for({-100, 100, 1200, 800}, monitors) == 1, "choose greatest overlap");
        require(monitor_for({3000, 0, 1000, 700}, monitors) == 1,
                "offscreen window chooses nearest monitor");
        require(monitor_for({0, -900, 1000, 700}, monitors) == 1,
                "offscreen monitor tie uses distance");
        std::cout << "13 window geometry checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
