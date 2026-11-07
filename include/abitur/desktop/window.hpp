// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "abitur/desktop/geometry.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace abitur::desktop {
struct WindowOptions {
    std::string title = "Abitur - ABI Compatibility Workbench";
    int width = 1280, height = 860;
    int min_width = 960, min_height = 640;
};
struct ChromeState {
    std::string activity = "Ready to compare";
    std::string detail = "ELF / DWARF";
    bool busy = false;
};

// Owns the OS surface, OpenGL context, ImGui backends, theme, font and window
// chrome. Application panels have no platform or GLFW dependencies.
class Window {
  public:
    explicit Window(WindowOptions options = {});
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool should_close() const;
    bool begin_frame();
    Rect draw_chrome(const ChromeState& state = {});
    void end_frame();
    void close();
    void minimize();
    void restore();
    void toggle_maximize();
    bool maximized() const;
    bool decorated() const;
    Rect bounds() const;
    void set_bounds(Rect rect);
    void set_drop_handler(std::function<void(const std::vector<std::string>&)> handler);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace abitur::desktop
