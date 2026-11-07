// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "abitur/desktop/window.hpp"
#include "embedded_font.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace abitur::desktop {
namespace {
constexpr float title_height = 48;
constexpr float footer_height = 26;
constexpr int border = 6;
constexpr ImVec4 background{0.075f, 0.082f, 0.098f, 1};
constexpr ImVec4 surface{0.10f, 0.11f, 0.13f, 1};
constexpr ImVec4 accent{0.42f, 0.84f, 0.71f, 1};
constexpr ImVec4 muted{0.54f, 0.58f, 0.64f, 1};

void theme() {
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.WindowPadding = {16, 16};
    s.FramePadding = {10, 7};
    s.ItemSpacing = {10, 9};
    s.WindowRounding = 0;
    s.ChildRounding = 3;
    s.FrameRounding = 3;
    s.GrabRounding = 3;
    s.WindowBorderSize = 1;
    s.ChildBorderSize = 1;
    s.ScrollbarSize = 12;
    s.Colors[ImGuiCol_WindowBg] = background;
    s.Colors[ImGuiCol_ChildBg] = surface;
    s.Colors[ImGuiCol_PopupBg] = {0.10f, 0.11f, 0.14f, 1};
    s.Colors[ImGuiCol_Border] = {0.23f, 0.26f, 0.31f, 1};
    s.Colors[ImGuiCol_Text] = {0.89f, 0.91f, 0.94f, 1};
    s.Colors[ImGuiCol_TextDisabled] = muted;
    s.Colors[ImGuiCol_FrameBg] = {0.14f, 0.16f, 0.19f, 1};
    s.Colors[ImGuiCol_FrameBgHovered] = {0.19f, 0.23f, 0.27f, 1};
    s.Colors[ImGuiCol_Button] = {0.18f, 0.22f, 0.26f, 1};
    s.Colors[ImGuiCol_ButtonHovered] = {0.23f, 0.35f, 0.33f, 1};
    s.Colors[ImGuiCol_ButtonActive] = {0.22f, 0.42f, 0.37f, 1};
    s.Colors[ImGuiCol_Header] = {0.16f, 0.30f, 0.27f, 1};
    s.Colors[ImGuiCol_HeaderHovered] = {0.21f, 0.37f, 0.32f, 1};
    s.Colors[ImGuiCol_HeaderActive] = {0.24f, 0.42f, 0.36f, 1};
    s.Colors[ImGuiCol_TabSelected] = {0.17f, 0.31f, 0.28f, 1};
    s.Colors[ImGuiCol_CheckMark] = accent;
    s.Colors[ImGuiCol_Separator] = s.Colors[ImGuiCol_Border];
    s.Colors[ImGuiCol_NavCursor] = accent;
}

ImGuiMouseCursor cursor_for(Edge edge) {
    if ((has(edge, Edge::left) && has(edge, Edge::top)) ||
        (has(edge, Edge::right) && has(edge, Edge::bottom)))
        return ImGuiMouseCursor_ResizeNWSE;
    if ((has(edge, Edge::right) && has(edge, Edge::top)) ||
        (has(edge, Edge::left) && has(edge, Edge::bottom)))
        return ImGuiMouseCursor_ResizeNESW;
    return has(edge, Edge::left) || has(edge, Edge::right) ? ImGuiMouseCursor_ResizeEW
                                                           : ImGuiMouseCursor_ResizeNS;
}

#if defined(__APPLE__)
constexpr bool is_apple = true;
#else
constexpr bool is_apple = false;
#endif

enum class Control { minimize, maximize, close };

bool traffic_light(Control kind, ImVec2 center, float radius, bool maximized) {
    ImVec2 min{center.x - radius - 3, center.y - radius - 3};
    ImVec2 size{radius * 2 + 6, radius * 2 + 6};
    ImGui::SetCursorPos(min);
    const char* id = kind == Control::close      ? "##mac_close"
                     : kind == Control::minimize ? "##mac_minimize"
                                                 : "##mac_maximize";
    bool pressed = ImGui::InvisibleButton(id, size);
    auto* draw = ImGui::GetWindowDrawList();
    bool hover = ImGui::IsItemHovered();

    ImU32 fill, border, symbol_color;
    if (kind == Control::close) {
        fill = hover ? IM_COL32(255, 95, 86, 255) : IM_COL32(235, 86, 79, 230);
        border = IM_COL32(224, 68, 62, 255);
        symbol_color = IM_COL32(75, 10, 10, 230);
    } else if (kind == Control::minimize) {
        fill = hover ? IM_COL32(255, 189, 46, 255) : IM_COL32(235, 172, 38, 230);
        border = IM_COL32(222, 161, 35, 255);
        symbol_color = IM_COL32(130, 75, 0, 230);
    } else {
        fill = hover ? IM_COL32(39, 201, 63, 255) : IM_COL32(35, 182, 57, 230);
        border = IM_COL32(26, 171, 41, 255);
        symbol_color = IM_COL32(10, 85, 15, 230);
    }

    draw->AddCircleFilled(center, radius, fill, 16);
    draw->AddCircle(center, radius, border, 16, 0.9f);

    if (hover) {
        if (kind == Control::close) {
            draw->AddLine({center.x - 3, center.y - 3}, {center.x + 3, center.y + 3}, symbol_color, 1.2f);
            draw->AddLine({center.x + 3, center.y - 3}, {center.x - 3, center.y + 3}, symbol_color, 1.2f);
        } else if (kind == Control::minimize) {
            draw->AddLine({center.x - 3.5f, center.y}, {center.x + 3.5f, center.y}, symbol_color, 1.4f);
        } else if (maximized) {
            draw->AddLine({center.x - 3, center.y - 3}, {center.x + 3, center.y + 3}, symbol_color, 1.2f);
            draw->AddLine({center.x - 3, center.y + 3}, {center.x + 3, center.y - 3}, symbol_color, 1.2f);
        } else {
            draw->AddLine({center.x - 3.5f, center.y}, {center.x + 3.5f, center.y}, symbol_color, 1.2f);
            draw->AddLine({center.x, center.y - 3.5f}, {center.x, center.y + 3.5f}, symbol_color, 1.2f);
        }
        ImGui::SetTooltip("%s", kind == Control::close      ? "Close"
                                : kind == Control::minimize ? "Minimize"
                                : maximized                 ? "Restore"
                                                            : "Zoom");
    }
    return pressed;
}

bool control(Control kind, ImVec2 pos, bool maximized) {
    ImGui::SetCursorPos(pos);
    const char* id = kind == Control::minimize   ? "##minimize"
                     : kind == Control::maximize ? "##maximize"
                                                 : "##close";
    bool pressed = ImGui::InvisibleButton(id, {44, title_height - border});
    auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    auto* draw = ImGui::GetWindowDrawList();
    bool hover = ImGui::IsItemHovered();
    if (hover || ImGui::IsItemActive()) {
        auto fill = kind == Control::close ? IM_COL32(180, 57, 65, 255) : IM_COL32(57, 69, 81, 255);
        draw->AddRectFilled(min, max, fill, 3);
    }
    if (ImGui::IsItemFocused())
        draw->AddRect(min, max, ImGui::GetColorU32(accent), 3);
    auto c = ImVec2{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    auto ink = IM_COL32(221, 227, 236, 255);
    if (kind == Control::minimize)
        draw->AddLine({c.x - 6, c.y + 3}, {c.x + 6, c.y + 3}, ink, 1.5f);
    else if (kind == Control::close) {
        draw->AddLine({c.x - 5, c.y - 5}, {c.x + 5, c.y + 5}, ink, 1.5f);
        draw->AddLine({c.x + 5, c.y - 5}, {c.x - 5, c.y + 5}, ink, 1.5f);
    } else if (maximized) {
        draw->AddRect({c.x - 3, c.y - 6}, {c.x + 6, c.y + 3}, ink);
        draw->AddRectFilled(
            {c.x - 6, c.y - 3}, {c.x + 3, c.y + 6},
            ImGui::GetColorU32(hover ? ImVec4{0.22f, 0.27f, 0.32f, 1} : background));
        draw->AddRect({c.x - 6, c.y - 3}, {c.x + 3, c.y + 6}, ink);
    } else
        draw->AddRect({c.x - 5, c.y - 5}, {c.x + 5, c.y + 5}, ink);
    if (hover)
        ImGui::SetTooltip("%s", kind == Control::close      ? "Close"
                                : kind == Control::minimize ? "Minimize"
                                : maximized                 ? "Restore"
                                                            : "Maximize");
    return pressed;
}
} // namespace

struct Window::Impl {
    WindowOptions options;
    GLFWwindow* handle = nullptr;
    ImGuiContext* context = nullptr;
    bool glfw_ready = false, glfw_backend = false, gl_backend = false;
    bool manual_maximized = false;
    Rect restore_rect;
    enum class Drag { none, move, resize } drag = Drag::none;
    Edge resize_edge = Edge::none;
    Rect drag_rect;
    Point drag_origin;
    Point drag_local;
    bool moved = false;
    float font_scale = 0;
    std::function<void(const std::vector<std::string>&)> drop;

    ~Impl() {
        if (context) {
            ImGui::SetCurrentContext(context);
            if (handle)
                glfwMakeContextCurrent(handle);
            if (gl_backend)
                ImGui_ImplOpenGL3_Shutdown();
            if (glfw_backend)
                ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext(context);
        }
        if (handle)
            glfwDestroyWindow(handle);
        if (glfw_ready)
            glfwTerminate();
    }
    Rect bounds() const {
        Rect result;
        glfwGetWindowPos(handle, &result.x, &result.y);
        glfwGetWindowSize(handle, &result.width, &result.height);
        return result;
    }
    void apply(Rect rect) {
        glfwSetWindowPos(handle, rect.x, rect.y);
        glfwSetWindowSize(handle, rect.width, rect.height);
    }
    // Native maximize attributes are inconsistent for undecorated surfaces
    // (notably Cocoa's isZoomed). All three platforms use our work-area state.
    bool maximized() const {
        return manual_maximized;
    }
    Point pointer() const {
        // Consume the same event stream as the pressed ImGui item. Native
        // cursor polling can be ahead of queued input (or refer to a different
        // pointer position when an inactive window receives injected events).
        auto pos = ImGui::GetIO().MousePos;
        auto rect = bounds();
        return {rect.x + pos.x, rect.y + pos.y};
    }
    void fonts() {
        float x = 1, y = 1;
        glfwGetWindowContentScale(handle, &x, &y);
        // Rebuild only between frames when crossing monitors with different DPI.
        auto scale = std::clamp(std::max(x, y), 1.0f, 4.0f);
        if (std::abs(scale - font_scale) < 0.05f)
            return;
        if (gl_backend)
            ImGui_ImplOpenGL3_DestroyFontsTexture();
        auto& io = ImGui::GetIO();
        io.Fonts->Clear();
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        config.OversampleH = 2;
        config.OversampleV = 2;
        static const ImWchar ranges[] = {0x0020, 0x00ff, 0x011e, 0x011f, 0x0130,
                                         0x0131, 0x015e, 0x015f, 0};
        if (!io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::font),
                                            sizeof(assets::font), 16 * scale, &config, ranges))
            throw std::runtime_error("Cannot load the embedded desktop font");
        io.FontGlobalScale = 1.0f / scale;
        font_scale = scale;
    }
    void initialize() {
        glfwSetErrorCallback(
            [](int, const char* message) { std::cerr << "GLFW: " << message << '\n'; });
#if defined(__linux__)
#if GLFW_VERSION_MAJOR > 3 || GLFW_VERSION_MINOR >= 4
        if (!glfwPlatformSupported(GLFW_PLATFORM_X11))
            throw std::runtime_error(
                "Borderless desktop controls require GLFW X11 support (X11 or XWayland)");
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#else
        if (std::string(glfwGetVersionString()).find("Wayland") != std::string::npos)
            throw std::runtime_error(
                "Use an X11-enabled GLFW build for the shared borderless desktop window");
#endif
#endif
        if (!glfwInit())
            throw std::runtime_error("Cannot initialize the desktop window system");
        glfw_ready = true;
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        handle = glfwCreateWindow(options.width, options.height, options.title.c_str(), nullptr,
                                  nullptr);
        if (!handle)
            throw std::runtime_error("Cannot create the OpenGL 3.2 desktop window");
        glfwSetWindowSizeLimits(handle, options.min_width, options.min_height, GLFW_DONT_CARE,
                                GLFW_DONT_CARE);
        glfwMakeContextCurrent(handle);
        glfwSwapInterval(1);
        restore_rect = bounds();
        IMGUI_CHECKVERSION();
        context = ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
        theme();
        fonts();
        glfw_backend = ImGui_ImplGlfw_InitForOpenGL(handle, true);
        if (!glfw_backend)
            throw std::runtime_error("Cannot initialize ImGui GLFW backend");
        gl_backend = ImGui_ImplOpenGL3_Init("#version 150");
        if (!gl_backend)
            throw std::runtime_error("Cannot initialize ImGui OpenGL backend");
        glfwSetWindowUserPointer(handle, this);
        glfwSetDropCallback(handle, [](GLFWwindow* window, int count, const char** paths) {
            auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(window));
            if (!self->drop)
                return;
            std::vector<std::string> files(paths, paths + count);
            self->drop(files);
        });
    }
    void maximize() {
        if (maximized()) {
            manual_maximized = false;
            apply(restore_rect);
            return;
        }
        restore_rect = bounds();
        int count = 0;
        auto** monitors = glfwGetMonitors(&count);
        if (!monitors || count == 0)
            return;
        std::vector<Rect> workareas;
        for (int i = 0; i < count; ++i) {
            Rect area;
            glfwGetMonitorWorkarea(monitors[i], &area.x, &area.y, &area.width, &area.height);
            workareas.push_back(area);
        }
        manual_maximized = true;
        apply(workareas[monitor_for(restore_rect, workareas)]);
    }
    void update_drag() {
        if (drag == Drag::none)
            return;
        bool released = !ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (!ImGui::IsMousePosValid() ||
            (released && !ImGui::IsMouseReleased(ImGuiMouseButton_Left))) {
            drag = Drag::none;
            return;
        }
        auto kind = drag;
        // A complete quick gesture can be delivered between two frames. Apply
        // its release position once before ending capture instead of dropping
        // the entire move/resize operation.
        if (released)
            drag = Drag::none;
        auto pos = pointer();
        int dx = static_cast<int>(std::round(pos.x - drag_origin.x));
        int dy = static_cast<int>(std::round(pos.y - drag_origin.y));
        if (kind == Drag::resize) {
            ImGui::SetMouseCursor(cursor_for(resize_edge));
            apply(resize(drag_rect, resize_edge, dx, dy, options.min_width, options.min_height));
        } else {
            if (!moved && std::abs(dx) + std::abs(dy) < 4)
                return;
            if (!moved && maximized()) {
                double fraction = std::clamp(drag_local.x / drag_rect.width, 0.0, 1.0);
                manual_maximized = false;
                auto restored = restore_rect;
                restored.x = static_cast<int>(std::round(pos.x - restored.width * fraction));
                restored.y = static_cast<int>(std::round(pos.y - drag_local.y));
                apply(restored);
                drag_rect = restored;
                drag_origin = pos;
                dx = dy = 0;
            }
            moved = true;
            glfwSetWindowPos(handle, drag_rect.x + dx, drag_rect.y + dy);
        }
    }
    void start_drag(Drag kind, Edge edge = Edge::none) {
        drag = kind;
        resize_edge = edge;
        drag_rect = bounds();
        // Use the press position processed by ImGui. Querying GLFW here can
        // already return the end of a fast gesture queued before this frame.
        const auto pressed = ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left];
        drag_origin = {drag_rect.x + pressed.x, drag_rect.y + pressed.y};
        drag_local = {drag_origin.x - drag_rect.x, drag_origin.y - drag_rect.y};
        moved = false;
    }
};

Window::Window(WindowOptions options) : impl_(std::make_unique<Impl>()) {
    impl_->options = std::move(options);
    impl_->initialize();
}
Window::~Window() = default;
bool Window::should_close() const {
    return glfwWindowShouldClose(impl_->handle);
}
bool Window::maximized() const {
    return impl_->maximized();
}
bool Window::decorated() const {
    return glfwGetWindowAttrib(impl_->handle, GLFW_DECORATED);
}
Rect Window::bounds() const {
    return impl_->bounds();
}
void Window::set_bounds(Rect rect) {
    impl_->manual_maximized = false;
    impl_->apply(rect);
}
void Window::close() {
    glfwSetWindowShouldClose(impl_->handle, GLFW_TRUE);
}
void Window::minimize() {
    impl_->drag = Impl::Drag::none;
    glfwIconifyWindow(impl_->handle);
}
void Window::restore() {
    if (glfwGetWindowAttrib(impl_->handle, GLFW_ICONIFIED))
        glfwRestoreWindow(impl_->handle);
    if (impl_->maximized())
        impl_->maximize();
}
void Window::toggle_maximize() {
    impl_->drag = Impl::Drag::none;
    impl_->maximize();
}
void Window::set_drop_handler(std::function<void(const std::vector<std::string>&)> handler) {
    impl_->drop = std::move(handler);
}

bool Window::begin_frame() {
    glfwPollEvents();
    int width = 0, height = 0;
    glfwGetFramebufferSize(impl_->handle, &width, &height);
    if (!width || !height || glfwGetWindowAttrib(impl_->handle, GLFW_ICONIFIED)) {
        glfwWaitEventsTimeout(0.05);
        return false;
    }
    ImGui::SetCurrentContext(impl_->context);
    impl_->fonts();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return true;
}

Rect Window::draw_chrome(const ChromeState& state) {
    auto size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##desktop-chrome", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    auto* draw = ImGui::GetWindowDrawList();
    bool focused = glfwGetWindowAttrib(impl_->handle, GLFW_FOCUSED);
    auto border_color = focused ? IM_COL32(76, 115, 111, 255) : IM_COL32(52, 58, 68, 255);
    draw->AddRectFilled({0, 0}, {size.x, title_height}, ImGui::GetColorU32(surface));
    draw->AddLine({0, 1.0f}, {size.x, 1.0f}, IM_COL32(255, 255, 255, 18));
    draw->AddRect({0.5f, 0.5f}, {size.x - 0.5f, size.y - 0.5f}, border_color);
    draw->AddLine({border, title_height}, {size.x - border, title_height},
                  ImGui::GetColorU32(ImGuiCol_Border));
    draw->AddRectFilled({border, size.y - footer_height}, {size.x - border, size.y - border},
                        ImGui::GetColorU32(surface));
    draw->AddCircleFilled({19, size.y - 15}, 3,
                          ImGui::GetColorU32(state.busy ? ImVec4{0.96f, 0.73f, 0.38f, 1} : accent));
    draw->AddText({29, size.y - 23}, ImGui::GetColorU32(muted), state.activity.c_str());
    auto detail_width = ImGui::CalcTextSize(state.detail.c_str()).x;
    draw->AddText({size.x - detail_width - 18, size.y - 23}, ImGui::GetColorU32(muted),
                  state.detail.c_str());

    float drag_x = is_apple ? 78.0f : border;
    float drag_w = is_apple ? std::max(1.0f, size.x - drag_x - border)
                            : std::max(1.0f, size.x - 2 * border - 3 * 44);
    ImGui::SetCursorPos({drag_x, border});
    ImGui::InvisibleButton("##title-drag", {drag_w, title_height - border});
    bool double_click =
        ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (double_click)
        toggle_maximize();
    else if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        impl_->start_drag(Impl::Drag::move);

    if constexpr (is_apple) {
        if (traffic_light(Control::close, {20, title_height * 0.5f}, 6.0f, maximized()))
            close();
        if (traffic_light(Control::minimize, {40, title_height * 0.5f}, 6.0f, maximized()))
            minimize();
        if (traffic_light(Control::maximize, {60, title_height * 0.5f}, 6.0f, maximized()))
            toggle_maximize();
    } else {
        if (control(Control::minimize, {size.x - border - 3 * 44, border}, maximized()))
            minimize();
        if (control(Control::maximize, {size.x - border - 2 * 44, border}, maximized()))
            toggle_maximize();
        if (control(Control::close, {size.x - border - 44, border}, maximized()))
            close();
    }

    float brand_x = is_apple ? 88.0f : 18.0f;
    auto ink = ImGui::GetColorU32(accent);
    draw->AddRectFilled({brand_x, 14}, {brand_x + 20, 34}, IM_COL32(24, 46, 40, 255), 4);
    draw->AddRect({brand_x, 14}, {brand_x + 20, 34}, ink, 4, 0, 1.2f);
    draw->AddLine({brand_x + 6, 20}, {brand_x + 6, 28}, ink, 1.5f);
    draw->AddLine({brand_x + 10, 17}, {brand_x + 10, 28}, ink, 1.5f);
    draw->AddLine({brand_x + 14, 23}, {brand_x + 14, 28}, ink, 1.5f);

    draw->AddText({brand_x + 28, 15}, ImGui::GetColorU32(ImGuiCol_Text), "ABITUR");
    draw->AddText({brand_x + 88, 15}, ImGui::GetColorU32(muted), "|");
    draw->AddText({brand_x + 100, 15}, ImGui::GetColorU32(muted), "ELF Analyzer & ABI Workbench");

    if (!state.detail.empty()) {
        auto tag_text = state.detail.c_str();
        auto tag_w = ImGui::CalcTextSize(tag_text).x;
        float pill_x = (is_apple ? size.x - tag_w - 32 : size.x - 3 * 44 - tag_w - 24);
        if (pill_x > brand_x + 360) {
            draw->AddRectFilled({pill_x - 8, 14}, {pill_x + tag_w + 8, 34}, IM_COL32(28, 34, 42, 255), 4);
            draw->AddRect({pill_x - 8, 14}, {pill_x + tag_w + 8, 34}, IM_COL32(50, 58, 70, 255), 4);
            draw->AddText({pill_x, 15}, ImGui::GetColorU32(accent), tag_text);
        }
    }

    bool modal =
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!modal && !maximized()) {
        struct Handle {
            ImVec2 position;
            ImVec2 size;
            Edge edge;
        };
        const Handle handles[] = {
            {{0, border}, {border, size.y - 2 * border}, Edge::left},
            {{size.x - border, border}, {border, size.y - 2 * border}, Edge::right},
            {{border, 0}, {size.x - 2 * border, border}, Edge::top},
            {{border, size.y - border}, {size.x - 2 * border, border}, Edge::bottom},
            {{0, 0}, {border, border}, Edge::left | Edge::top},
            {{size.x - border, 0}, {border, border}, Edge::right | Edge::top},
            {{0, size.y - border}, {border, border}, Edge::left | Edge::bottom},
            {{size.x - border, size.y - border}, {border, border}, Edge::right | Edge::bottom},
        };
        for (int i = 0; i < 8; ++i) {
            ImGui::PushID(i);
            ImGui::SetCursorPos(handles[i].position);
            // Owning the active ImGui ID keeps empty-window move handling from
            // swallowing a border press and preserves capture across the body.
            ImGui::InvisibleButton("##resize", handles[i].size);
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(cursor_for(handles[i].edge));
            if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                impl_->start_drag(Impl::Drag::resize, handles[i].edge);
            ImGui::PopID();
        }
    }
    if (modal)
        impl_->drag = Impl::Drag::none;
    impl_->update_drag();
    ImGui::End();
    ImGui::PopStyleVar(2);
    return {border, static_cast<int>(title_height + 1), static_cast<int>(size.x) - 2 * border,
            static_cast<int>(size.y - title_height - footer_height - 1)};
}

void Window::end_frame() {
    ImGui::Render();
    int width = 0, height = 0;
    glfwGetFramebufferSize(impl_->handle, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(background.x, background.y, background.z, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(impl_->handle);
}
} // namespace abitur::desktop
