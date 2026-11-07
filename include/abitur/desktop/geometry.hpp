// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <algorithm>
#include <cstdint>
#include <span>

namespace abitur::desktop {
struct Point {
    double x = 0;
    double y = 0;
};
struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
    bool operator==(const Rect&) const = default;
};
enum class Edge : unsigned { none = 0, left = 1, right = 2, top = 4, bottom = 8 };
constexpr Edge operator|(Edge a, Edge b) {
    return static_cast<Edge>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr bool has(Edge edges, Edge edge) {
    return (static_cast<unsigned>(edges) & static_cast<unsigned>(edge)) != 0;
}

// Coordinates, minimum sizes and drag deltas all use GLFW screen units, never
// framebuffer pixels. Retina/HiDPI scaling therefore cannot double drag deltas.
inline Edge hit_test(Point local, int width, int height, int border = 6) {
    if (local.x < 0 || local.y < 0 || local.x >= width || local.y >= height)
        return Edge::none;
    Edge result = Edge::none;
    if (local.x < border)
        result = result | Edge::left;
    else if (local.x >= width - border)
        result = result | Edge::right;
    if (local.y < border)
        result = result | Edge::top;
    else if (local.y >= height - border)
        result = result | Edge::bottom;
    return result;
}

inline Rect resize(Rect start, Edge edge, int dx, int dy, int min_width, int min_height) {
    auto result = start;
    if (has(edge, Edge::left)) {
        result.width = std::max(min_width, start.width - dx);
        result.x = start.x + start.width - result.width;
    } else if (has(edge, Edge::right))
        result.width = std::max(min_width, start.width + dx);
    if (has(edge, Edge::top)) {
        result.height = std::max(min_height, start.height - dy);
        result.y = start.y + start.height - result.height;
    } else if (has(edge, Edge::bottom))
        result.height = std::max(min_height, start.height + dy);
    return result;
}

inline std::size_t monitor_for(Rect window, std::span<const Rect> monitors) {
    std::size_t best = 0;
    std::int64_t best_overlap = -1;
    double best_distance = 0;
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        auto m = monitors[i];
        auto width =
            std::max(0, std::min(window.x + window.width, m.x + m.width) - std::max(window.x, m.x));
        auto height = std::max(0, std::min(window.y + window.height, m.y + m.height) -
                                      std::max(window.y, m.y));
        auto overlap = static_cast<std::int64_t>(width) * height;
        auto dx = (window.x + window.width * 0.5) - (m.x + m.width * 0.5);
        auto dy = (window.y + window.height * 0.5) - (m.y + m.height * 0.5);
        auto distance = dx * dx + dy * dy;
        if (overlap > best_overlap || (overlap == best_overlap && distance < best_distance)) {
            best = i;
            best_overlap = overlap;
            best_distance = distance;
        }
    }
    return best;
}
} // namespace abitur::desktop
