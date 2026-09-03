#include "EditorIcons.hpp"
#include "../EditorTheme.hpp"

#include "../../core/Log.hpp"
#include <SDL3/SDL_filesystem.h>
#include <algorithm>
#include <array>
#include <filesystem>

namespace Engine {

namespace {

bool g_editor_icons_loaded = false;

struct IconGlyph {
    ImWchar codepoint;
    const char* utf8;
};

constexpr std::array<IconGlyph, static_cast<std::size_t>(EditorIcon::Count)> ICON_GLYPHS{{
    {0xE121, "\xee\x84\xa1"}, // move
    {0xE149, "\xee\x85\x89"}, // rotate-cw
    {0xE2EC, "\xee\x8b\xac"}, // scaling
    {0xE1F3, "\xee\x87\xb3"}, // globe-2
    {0xE2FE, "\xee\x8b\xbe"}, // axis-3d
    {0xE0E9, "\xee\x83\xa9"}, // grid-3x3
    {0xE2B5, "\xee\x8a\xb5"}, // magnet
    {0xE245, "\xee\x89\x85"}, // settings-2
    {0xE064, "\xee\x81\xa4"}, // camera
    {0xE0B6, "\xee\x82\xb6"}, // ellipsis
    {0xE18E, "\xee\x86\x8e"}, // trash-2
    {0xE0F6, "\xee\x83\xb6"}, // image
    {0xE061, "\xee\x81\xa1"}, // box
    {0xE2D0, "\xee\x8b\x90"}, // boxes
    {0xE0D0, "\xee\x83\x90"}, // film
    {0xE0BA, "\xee\x82\xba"}, // eye
    {0xE0BB, "\xee\x82\xbb"}, // eye-off
    {0xE13D, "\xee\x84\xbd"}, // plus
    {0xE2AD, "\xee\x8a\xad"}, // component
    {0xE10B, "\xee\x84\x8b"}, // lock
    {0xE10C, "\xee\x84\x8c"}, // unlock / lock-open
    {0xE13C, "\xee\x84\xbc"}, // play
    {0xE12E, "\xee\x84\xae"}, // pause
    {0xE167, "\xee\x85\xa7"}, // square
    {0xE1B4, "\xee\x86\xb4"}, // zap
    {0xE0B9, "\xee\x82\xb9"}, // external-link
    {0xE1DD, "\xee\x87\x9d"}, // palette
    {0xE1B2, "\xee\x86\xb2"}, // x
    {0xE09E, "\xee\x82\x9e"}, // copy
    {0xE04A, "\xee\x81\x8a"}, // arrow-up
    {0xE048, "\xee\x81\x88"}, // arrow-left
    {0xE1F5, "\xee\x87\xb5"}, // history
    {0xE172, "\xee\x85\xb2"}, // square-pen
    {0xE158, "\xee\x85\x98"}, // shield
    {0xE1BB, "\xee\x86\xbb"}, // bot
    {0xE14D, "\xee\x85\x8d"}, // save
    {0xE3EA, "\xee\x8f\xaa"}  // step-forward
}};

const IconGlyph& icon_glyph(EditorIcon icon) {
    const auto index = static_cast<std::size_t>(icon);
    static constexpr IconGlyph fallback{static_cast<ImWchar>('?'), "?"};
    return index < ICON_GLYPHS.size() ? ICON_GLYPHS[index] : fallback;
}

std::filesystem::path locate_icon_font() {
    const char* base_path = SDL_GetBasePath();
    std::array<std::filesystem::path, 3> candidates{
        std::filesystem::path(base_path ? base_path : "") / "editor_assets/lucide/lucide.ttf",
        std::filesystem::path("editor_assets/lucide/lucide.ttf"),
        std::filesystem::path("vendor/lucide/lucide.ttf")
    };
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
    return {};
}

} // namespace

bool initialize_editor_icons() {
    ImGuiIO& io = ImGui::GetIO();
    if (g_editor_icons_loaded) return true;

    const std::filesystem::path font_path = locate_icon_font();
    if (font_path.empty()) {
        ENGINE_LOG_WARN("[EDITOR ICONS] Lucide font not found; editor icons will use fallback glyphs.");
        return false;
    }

    if (io.Fonts->Fonts.empty()) {
        ImFontConfig default_config;
        default_config.SizePixels = 13.0f;
        io.Fonts->AddFontDefault(&default_config);
    }
    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;
    config.GlyphMinAdvanceX = 12.0f;
    // Lucide's geometric box sits above ImGui's default text baseline. Two
    // logical pixels align inline icons in headers and menu labels. Icon-only
    // buttons use their baked visual bounds, so this offset does not displace
    // their optical center.
    config.GlyphOffset.y = 2.0f;
    // Rasterize only registered glyphs into ImGui's atlas. The vendored font
    // and codepoint catalog remain complete, so extending EditorIcon is cheap.
    static constexpr ImWchar icon_ranges[] = {
        0xE048, 0xE04A, 0xE061, 0xE061, 0xE064, 0xE064, 0xE09E, 0xE09E,
        0xE0B6, 0xE0BB, 0xE0D0, 0xE0D0, 0xE0E9, 0xE0E9,
        0xE0F6, 0xE0F6,
        0xE10B, 0xE10C, 0xE121, 0xE121, 0xE12E, 0xE12E,
        0xE13C, 0xE13D, 0xE149, 0xE14D, 0xE158, 0xE158,
        0xE167, 0xE167, 0xE172, 0xE172,
        0xE18E, 0xE18E, 0xE1B2, 0xE1B4, 0xE1BB, 0xE1BB, 0xE1DD, 0xE1DD,
        0xE1F3, 0xE1F5,
        0xE245, 0xE245, 0xE2AD, 0xE2AD, 0xE2B5, 0xE2B5,
        0xE2D0, 0xE2D0, 0xE2EC, 0xE2EC, 0xE2FE, 0xE2FE,
        0xE3EA, 0xE3EA,
        0
    };
    g_editor_icons_loaded = io.Fonts->AddFontFromFileTTF(
        font_path.string().c_str(), 14.0f, &config, icon_ranges
    ) != nullptr;

    if (g_editor_icons_loaded) {
        ENGINE_LOG_INFO("[EDITOR ICONS] Loaded Lucide editor glyphs from '{}'.", font_path.string());
    } else {
        ENGINE_LOG_WARN("[EDITOR ICONS] Failed to load Lucide font '{}'.", font_path.string());
    }
    return g_editor_icons_loaded;
}

const char* editor_icon_glyph(EditorIcon icon) {
    return icon_glyph(icon).utf8;
}

std::string editor_icon_label(EditorIcon icon, const char* label) {
    return std::string(editor_icon_glyph(icon)) + "  " + label;
}

void draw_editor_icon(ImDrawList* draw_list, EditorIcon icon, ImVec2 center, float size, ImU32 color) {
    ImFont* font = ImGui::GetFont();
    const ImWchar codepoint = g_editor_icons_loaded ? icon_glyph(icon).codepoint : static_cast<ImWchar>('?');
    ImFontBaked* baked = font->GetFontBaked(size);
    const ImFontGlyph* glyph = baked->FindGlyph(codepoint);
    if (!glyph || !glyph->Visible) return;

    const float scale = size / baked->Size;
    const ImVec2 origin(
        center.x - (glyph->X0 + glyph->X1) * scale * 0.5f,
        center.y - (glyph->Y0 + glyph->Y1) * scale * 0.5f
    );
    font->RenderChar(draw_list, size, origin, color, codepoint);
}

bool editor_icon_button(
    const char* id,
    EditorIcon icon,
    const char* tooltip,
    bool active,
    ImVec2 hit_size,
    float icon_size
) {
    const auto& palette = EditorTheme::palette();
    icon_size = std::clamp(
        icon_size * EditorTheme::metrics().scale,
        1.0f,
        std::max(1.0f, std::min(hit_size.x, hit_size.y) - 2.0f)
    );

    ImVec4 icon_color = active ? palette.accent_hovered : palette.text_muted;
    if (icon == EditorIcon::Hidden && active) {
        icon_color = ImVec4(0.88f, 0.51f, 0.24f, 0.94f);
    }

    ImGui::PushID(id);
    const bool pressed = ImGui::InvisibleButton("##button", hit_size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (hovered || active) {
        const int alpha = hovered ? 28 : 14;
        draw_list->AddRectFilled(
            minimum, maximum, IM_COL32(255, 255, 255, alpha),
            EditorTheme::metrics().small_rounding
        );
    }

    // Center using the actual rendered contour rather than the font advance.
    // Lucide sits optically high inside compact controls, so all icon buttons
    // share one theme-level baseline correction.
    ImVec2 icon_center(
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f +
            EditorTheme::metrics().icon_button_offset_y
    );
    draw_editor_icon(
        draw_list,
        icon,
        icon_center,
        icon_size,
        ImGui::ColorConvertFloat4ToU32(icon_color)
    );

    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return pressed;
}

} // namespace Engine
