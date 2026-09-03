#pragma once

#include <imgui.h>

namespace Engine {

struct EditorUiPalette {
    ImVec4 window;
    ImVec4 panel;
    ImVec4 panel_raised;
    ImVec4 header;
    ImVec4 field;
    ImVec4 border;
    ImVec4 text;
    ImVec4 text_muted;
    ImVec4 accent;
    ImVec4 accent_hovered;
    ImVec4 accent_active;
    ImVec4 selected;
    ImVec4 success;
    ImVec4 warning;
    ImVec4 error;
};

struct EditorUiMetrics {
    float scale{1.0f};
    float menu_height{24.0f};
    float status_height{24.0f};
    float area_header_height{28.0f};
    float toolbar_height{30.0f};
    float row_height{24.0f};
    float icon_size{14.0f};
    float icon_button_offset_y{1.0f};
    float small_rounding{3.0f};
    float panel_padding{8.0f};
    float compact_spacing{4.0f};
};

class EditorTheme {
public:
    [[nodiscard]] static bool initialize_fonts(float scale = 1.0f);
    static void apply_dark_theme(float scale = 1.0f);

    [[nodiscard]] static const EditorUiPalette& palette();
    [[nodiscard]] static const EditorUiMetrics& metrics();
};

} // namespace Engine
