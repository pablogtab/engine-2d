#include "EditorTheme.hpp"
#include <imgui.h>

namespace Engine {

void EditorTheme::apply_dark_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    ImGui::GetIO().ConfigInputTextCursorBlink = true;

    // --- Modern Rounded Geometry ---
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(8.0f, 5.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing     = 21.0f;
    style.ScrollbarSize     = 14.0f;
    style.GrabMinSize       = 10.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 0.0f;

    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 5.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 5.0f;
    style.InputTextCursorSize = 2.0f;

    // --- Modern Dark Slate & Violet Accent Palette ---
    // Backgrounds
    colors[ImGuiCol_WindowBg]             = ImVec4(0.09f, 0.10f, 0.13f, 1.00f); // #171A21
    colors[ImGuiCol_ChildBg]              = ImVec4(0.12f, 0.13f, 0.17f, 1.00f); // #1E212B
    colors[ImGuiCol_PopupBg]              = ImVec4(0.14f, 0.15f, 0.20f, 0.98f); // #242733
    colors[ImGuiCol_Border]               = ImVec4(0.18f, 0.20f, 0.26f, 1.00f); // #2E3342
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Text & Headers
    colors[ImGuiCol_Text]                 = ImVec4(0.92f, 0.93f, 0.96f, 1.00f); // Clean light
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.48f, 0.51f, 0.60f, 1.00f);
    colors[ImGuiCol_InputTextCursor]      = ImVec4(0.49f, 0.73f, 1.00f, 1.00f);
    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);

    // Frame inputs (Sliders, Textboxes)
    colors[ImGuiCol_FrameBg]              = ImVec4(0.15f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.24f, 0.31f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.28f, 0.31f, 0.40f, 1.00f);

    // Tabs & Docking
    colors[ImGuiCol_Tab]                  = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.31f, 0.27f, 0.82f, 0.80f); // Violet glow
    colors[ImGuiCol_TabSelected]          = ImVec4(0.31f, 0.27f, 0.82f, 1.00f); // Indigo-Violet #4F46E5
    colors[ImGuiCol_TabSelectedOverline]  = ImVec4(0.49f, 0.46f, 0.95f, 1.00f); // #7C3AED
    colors[ImGuiCol_TabDimmed]            = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]    = ImVec4(0.20f, 0.22f, 0.29f, 1.00f);

    // Title Bars
    colors[ImGuiCol_TitleBg]              = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.09f, 0.10f, 0.13f, 0.75f);

    // Buttons
    colors[ImGuiCol_Button]               = ImVec4(0.27f, 0.24f, 0.73f, 0.85f); // #4338CA
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.35f, 0.31f, 0.89f, 1.00f); // #5950E3
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.23f, 0.20f, 0.64f, 1.00f);

    // Headers (Collapsing headers, tree nodes)
    colors[ImGuiCol_Header]               = ImVec4(0.22f, 0.24f, 0.32f, 0.70f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.31f, 0.27f, 0.82f, 0.80f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.31f, 0.27f, 0.82f, 1.00f);

    // Sliders & Handles
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.49f, 0.46f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.61f, 0.58f, 0.99f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.49f, 0.46f, 0.95f, 1.00f);

    // Separators & Resize Grips
    colors[ImGuiCol_Separator]            = ImVec4(0.18f, 0.20f, 0.26f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.31f, 0.27f, 0.82f, 0.78f);
    colors[ImGuiCol_SeparatorActive]      = ImVec4(0.31f, 0.27f, 0.82f, 1.00f);
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0.27f, 0.24f, 0.73f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.35f, 0.31f, 0.89f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.49f, 0.46f, 0.95f, 0.95f);

    // Docking
    colors[ImGuiCol_DockingPreview]       = ImVec4(0.31f, 0.27f, 0.82f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]       = ImVec4(0.08f, 0.08f, 0.11f, 1.00f);

    // Scrollbars
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.09f, 0.10f, 0.13f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.22f, 0.24f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.33f, 0.42f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.40f, 0.43f, 0.55f, 1.00f);
}

} // namespace Engine
