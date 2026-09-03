#include "EditorTheme.hpp"
#include "../core/Log.hpp"

#include <SDL3/SDL_filesystem.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <filesystem>

namespace Engine {

namespace {

EditorUiPalette g_palette{};
EditorUiMetrics g_metrics{};

// Inter contains private-use glyphs for OpenType alternates. Lucide uses the
// same Unicode block for icons, so the text source must explicitly yield that
// range to the icon source when both are merged into one ImGui font.
constexpr ImWchar EDITOR_ICON_PRIVATE_USE_RANGE[] = {
    0xE000, 0xF8FF,
    0
};

std::filesystem::path locate_editor_font() {
    const char* base_path = SDL_GetBasePath();
    const std::array<std::filesystem::path, 3> candidates{
        std::filesystem::path(base_path ? base_path : "") /
            "editor_assets/inter/InterVariable.ttf",
        std::filesystem::path("editor_assets/inter/InterVariable.ttf"),
        std::filesystem::path("vendor/inter/InterVariable.ttf")
    };
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
    return {};
}

float bounded_scale(float scale) {
    return std::clamp(scale, 0.75f, 2.5f);
}

} // namespace

bool EditorTheme::initialize_fonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float safe_scale = bounded_scale(scale);
    const std::filesystem::path font_path = locate_editor_font();
    if (!font_path.empty()) {
        ImFontConfig config;
        config.OversampleH = 2;
        config.OversampleV = 2;
        config.PixelSnapH = false;
        config.SizePixels = 14.0f * safe_scale;
        config.GlyphExcludeRanges = EDITOR_ICON_PRIVATE_USE_RANGE;
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            font_path.string().c_str(), config.SizePixels, &config,
            io.Fonts->GetGlyphRangesDefault()
        );
        if (font) {
            io.FontDefault = font;
            ENGINE_LOG_INFO("[EDITOR UI] Loaded Inter from '{}'.", font_path.string());
            return true;
        }
    }

    ImFontConfig fallback;
    fallback.SizePixels = 14.0f * safe_scale;
    io.FontDefault = io.Fonts->AddFontDefault(&fallback);
    ENGINE_LOG_WARN("[EDITOR UI] Inter not found; using the built-in font.");
    return false;
}

void EditorTheme::apply_dark_theme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    g_metrics = {};
    g_metrics.scale = bounded_scale(scale);
    g_metrics.menu_height *= g_metrics.scale;
    g_metrics.status_height *= g_metrics.scale;
    g_metrics.area_header_height *= g_metrics.scale;
    g_metrics.toolbar_height *= g_metrics.scale;
    g_metrics.row_height *= g_metrics.scale;
    g_metrics.icon_size *= g_metrics.scale;
    g_metrics.icon_button_offset_y *= g_metrics.scale;
    g_metrics.small_rounding *= g_metrics.scale;
    g_metrics.panel_padding *= g_metrics.scale;
    g_metrics.compact_spacing *= g_metrics.scale;

    // Neutral graphite surfaces keep the viewport dominant. Blue communicates
    // selection and interaction; amber is reserved for running/transient state.
    g_palette.window         = ImVec4(0.075f, 0.078f, 0.085f, 1.00f); // #131416
    g_palette.panel          = ImVec4(0.105f, 0.110f, 0.120f, 1.00f); // #1B1C1F
    g_palette.panel_raised   = ImVec4(0.135f, 0.140f, 0.153f, 1.00f); // #222427
    g_palette.header         = ImVec4(0.155f, 0.160f, 0.173f, 1.00f); // #28292C
    // Fields stay close to the panel value, but remain legible as editable
    // controls even when a dense Inspector is viewed at a glance.
    g_palette.field          = ImVec4(0.125f, 0.130f, 0.145f, 1.00f);
    g_palette.border         = ImVec4(0.235f, 0.245f, 0.265f, 1.00f);
    g_palette.text           = ImVec4(0.875f, 0.890f, 0.910f, 1.00f);
    g_palette.text_muted     = ImVec4(0.510f, 0.535f, 0.575f, 1.00f);
    g_palette.accent         = ImVec4(0.275f, 0.475f, 0.760f, 1.00f);
    g_palette.accent_hovered = ImVec4(0.340f, 0.555f, 0.860f, 1.00f);
    g_palette.accent_active  = ImVec4(0.225f, 0.400f, 0.675f, 1.00f);
    g_palette.selected       = ImVec4(0.220f, 0.365f, 0.560f, 1.00f);
    g_palette.success        = ImVec4(0.300f, 0.700f, 0.460f, 1.00f);
    g_palette.warning        = ImVec4(0.930f, 0.620f, 0.230f, 1.00f);
    g_palette.error          = ImVec4(0.880f, 0.330f, 0.350f, 1.00f);

    ImGui::GetIO().ConfigInputTextCursorBlink = true;

    // Compact desktop-editor geometry. Large rounding and saturated controls
    // make tools read like a dashboard instead of an authoring application.
    style.WindowPadding     = ImVec2(8.0f, 7.0f);
    style.FramePadding      = ImVec2(7.0f, 4.0f);
    style.ItemSpacing       = ImVec2(7.0f, 5.0f);
    style.ItemInnerSpacing  = ImVec2(5.0f, 4.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing     = 18.0f;
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 9.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 0.0f;

    style.WindowRounding    = 3.0f;
    style.ChildRounding     = 2.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 3.0f;
    style.InputTextCursorSize = 2.0f;

    style.ScaleAllSizes(g_metrics.scale);

    colors[ImGuiCol_WindowBg]             = g_palette.panel;
    colors[ImGuiCol_ChildBg]              = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_PopupBg]              = g_palette.panel_raised;
    colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.62f);
    colors[ImGuiCol_Border]               = g_palette.border;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_Text]                 = g_palette.text;
    colors[ImGuiCol_TextDisabled]         = g_palette.text_muted;
    colors[ImGuiCol_InputTextCursor]      = g_palette.accent_hovered;
    colors[ImGuiCol_MenuBarBg]            = g_palette.header;

    colors[ImGuiCol_FrameBg]              = g_palette.field;
    colors[ImGuiCol_FrameBgHovered]       = g_palette.panel_raised;
    colors[ImGuiCol_FrameBgActive]        = g_palette.selected;

    colors[ImGuiCol_Tab]                  = g_palette.panel_raised;
    colors[ImGuiCol_TabHovered]           = g_palette.selected;
    colors[ImGuiCol_TabSelected]          = g_palette.header;
    colors[ImGuiCol_TabSelectedOverline]  = g_palette.accent_hovered;
    colors[ImGuiCol_TabDimmed]            = g_palette.window;
    colors[ImGuiCol_TabDimmedSelected]    = g_palette.panel;

    colors[ImGuiCol_TitleBg]              = g_palette.panel;
    colors[ImGuiCol_TitleBgActive]        = g_palette.header;
    colors[ImGuiCol_TitleBgCollapsed]     = g_palette.window;

    colors[ImGuiCol_Button]               = g_palette.panel_raised;
    colors[ImGuiCol_ButtonHovered]        = g_palette.selected;
    colors[ImGuiCol_ButtonActive]         = g_palette.accent_active;

    colors[ImGuiCol_Header]               = g_palette.selected;
    colors[ImGuiCol_HeaderHovered]        = g_palette.accent;
    colors[ImGuiCol_HeaderActive]         = g_palette.accent_active;

    colors[ImGuiCol_SliderGrab]           = g_palette.accent;
    colors[ImGuiCol_SliderGrabActive]     = g_palette.accent_hovered;
    colors[ImGuiCol_CheckMark]            = g_palette.accent_hovered;

    colors[ImGuiCol_Separator]            = g_palette.border;
    colors[ImGuiCol_SeparatorHovered]     = g_palette.accent;
    colors[ImGuiCol_SeparatorActive]      = g_palette.accent_hovered;
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0.28f, 0.48f, 0.76f, 0.18f);
    colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.34f, 0.56f, 0.86f, 0.55f);
    colors[ImGuiCol_ResizeGripActive]     = g_palette.accent_hovered;

    colors[ImGuiCol_DockingPreview]       = ImVec4(0.28f, 0.48f, 0.76f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]       = g_palette.window;

    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.075f, 0.078f, 0.085f, 0.65f);
    colors[ImGuiCol_ScrollbarGrab]        = g_palette.border;
    colors[ImGuiCol_ScrollbarGrabHovered] = g_palette.text_muted;
    colors[ImGuiCol_ScrollbarGrabActive]  = g_palette.accent;

    colors[ImGuiCol_TableHeaderBg]        = g_palette.header;
    colors[ImGuiCol_TableBorderStrong]    = g_palette.border;
    colors[ImGuiCol_TableBorderLight]     = ImVec4(0.19f, 0.20f, 0.22f, 1.0f);
    colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.0f, 1.0f, 1.0f, 0.018f);
    colors[ImGuiCol_NavCursor]            = g_palette.accent_hovered;
    colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.28f, 0.48f, 0.76f, 0.45f);
}

const EditorUiPalette& EditorTheme::palette() {
    return g_palette;
}

const EditorUiMetrics& EditorTheme::metrics() {
    return g_metrics;
}

} // namespace Engine
