#include "EditorUi.hpp"

#include "../EditorTheme.hpp"

#include <algorithm>

namespace Engine::EditorUi {

namespace {

ImVec4 tone_color(BadgeTone tone) {
    const auto& palette = EditorTheme::palette();
    switch (tone) {
        case BadgeTone::Accent: return palette.accent_hovered;
        case BadgeTone::Success: return palette.success;
        case BadgeTone::Warning: return palette.warning;
        case BadgeTone::Error: return palette.error;
        case BadgeTone::Neutral: break;
    }
    return palette.text_muted;
}

ImVec4 with_alpha(const ImVec4& color, float alpha) {
    return ImVec4(color.x, color.y, color.z, alpha);
}

} // namespace

void section_heading(const char* label, const char* trailing) {
    const auto& palette = EditorTheme::palette();
    ImGui::PushStyleColor(ImGuiCol_Text, palette.text_muted);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (trailing) {
        const float trailing_width = ImGui::CalcTextSize(trailing).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - trailing_width));
        ImGui::TextDisabled("%s", trailing);
    }
    ImGui::Separator();
}

void badge(const char* label, BadgeTone tone) {
    const auto& metrics = EditorTheme::metrics();
    const ImVec4 color = tone_color(tone);
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const ImVec2 size(text_size.x + 10.0f * metrics.scale, text_size.y + 4.0f * metrics.scale);
    ImGui::Dummy(size);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        minimum, ImVec2(minimum.x + size.x, minimum.y + size.y),
        ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 0.15f)),
        metrics.small_rounding
    );
    draw_list->AddRect(
        minimum, ImVec2(minimum.x + size.x, minimum.y + size.y),
        ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 0.45f)),
        metrics.small_rounding
    );
    draw_list->AddText(
        ImVec2(minimum.x + 5.0f * metrics.scale, minimum.y + 2.0f * metrics.scale),
        ImGui::ColorConvertFloat4ToU32(color), label
    );
}

void empty_state(EditorIcon icon, const char* title, const char* detail) {
    const auto& metrics = EditorTheme::metrics();
    const auto& palette = EditorTheme::palette();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float block_height = detail ? 58.0f * metrics.scale : 40.0f * metrics.scale;
    const float top = origin.y + std::max(12.0f, (available.y - block_height) * 0.38f);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_editor_icon(
        draw_list, icon,
        ImVec2(origin.x + available.x * 0.5f, top + 8.0f * metrics.scale),
        20.0f * metrics.scale,
        ImGui::ColorConvertFloat4ToU32(palette.text_muted)
    );
    const ImVec2 title_size = ImGui::CalcTextSize(title);
    draw_list->AddText(
        ImVec2(origin.x + (available.x - title_size.x) * 0.5f, top + 23.0f * metrics.scale),
        ImGui::ColorConvertFloat4ToU32(palette.text), title
    );
    if (detail) {
        const ImVec2 detail_size = ImGui::CalcTextSize(detail);
        draw_list->AddText(
            ImVec2(origin.x + (available.x - detail_size.x) * 0.5f, top + 40.0f * metrics.scale),
            ImGui::ColorConvertFloat4ToU32(palette.text_muted), detail
        );
    }
    ImGui::Dummy(available);
}

bool toolbar_button(
    const char* id,
    EditorIcon icon,
    const char* tooltip,
    bool active,
    bool enabled
) {
    const auto& metrics = EditorTheme::metrics();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = editor_icon_button(
        id, icon, tooltip, active,
        ImVec2(metrics.toolbar_height - 6.0f * metrics.scale, metrics.toolbar_height - 6.0f * metrics.scale)
    );
    ImGui::EndDisabled();
    return enabled && pressed;
}

bool action_button(
    const char* id,
    EditorIcon icon,
    const char* label,
    ButtonTone tone,
    bool enabled,
    ImVec2 size
) {
    const auto& palette = EditorTheme::palette();
    ImVec4 base = palette.panel_raised;
    ImVec4 hovered = palette.header;
    ImVec4 active = palette.selected;
    switch (tone) {
        case ButtonTone::Primary:
            base = palette.accent;
            hovered = palette.accent_hovered;
            active = palette.accent_active;
            break;
        case ButtonTone::Success:
            base = with_alpha(palette.success, 0.78f);
            hovered = palette.success;
            active = with_alpha(palette.success, 0.66f);
            break;
        case ButtonTone::Danger:
            base = with_alpha(palette.error, 0.78f);
            hovered = palette.error;
            active = with_alpha(palette.error, 0.66f);
            break;
        case ButtonTone::Neutral:
            break;
    }

    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
    ImGui::BeginDisabled(!enabled);
    const std::string button_label = editor_icon_label(icon, label) + "##action";
    const bool pressed = ImGui::Button(button_label.c_str(), size);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    return enabled && pressed;
}

bool search_field(
    const char* id,
    const char* hint,
    char* buffer,
    std::size_t buffer_size,
    float width
) {
    if (width > 0.0f) ImGui::SetNextItemWidth(width);
    return ImGui::InputTextWithHint(id, hint, buffer, buffer_size);
}

void set_next_modal_width(float width) {
    const float scaled_width = width * EditorTheme::metrics().scale;
    const float available = ImGui::GetMainViewport()->WorkSize.x - 32.0f;
    ImGui::SetNextWindowSize(
        ImVec2(std::min(scaled_width, available), 0.0f), ImGuiCond_Appearing
    );
}

void modal_status(const char* label, BadgeTone tone) {
    badge(label, tone);
    ImGui::Spacing();
}

void modal_lead(const char* message, const char* detail) {
    const auto& palette = EditorTheme::palette();
    ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
    ImGui::TextWrapped("%s", message);
    ImGui::PopStyleColor();
    if (detail && detail[0] != '\0') {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, palette.text_muted);
        ImGui::TextWrapped("%s", detail);
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
}

void modal_resource(const char* label, const char* value, const char* detail) {
    const auto& palette = EditorTheme::palette();
    const auto& metrics = EditorTheme::metrics();
    const float height = (detail && detail[0] != '\0' ? 66.0f : 48.0f) * metrics.scale;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.panel_raised);
    ImGui::PushStyleColor(ImGuiCol_Border, palette.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, metrics.small_rounding);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(10.0f * metrics.scale, 7.0f * metrics.scale)
    );
    ImGui::BeginChild(
        "##modal_resource", ImVec2(0.0f, height), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    ImGui::TextDisabled("%s", label);
    ImGui::TextUnformatted(value);
    if (detail && detail[0] != '\0') ImGui::TextDisabled("%s", detail);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::Spacing();
}

void modal_callout(const char* message, BadgeTone tone) {
    const auto& palette = EditorTheme::palette();
    const auto& metrics = EditorTheme::metrics();
    const ImVec4 color = tone_color(tone);
    const float padding = 10.0f * metrics.scale;
    const float wrap_width = std::max(80.0f, ImGui::GetContentRegionAvail().x - padding * 2.0f);
    const ImVec2 text_size = ImGui::CalcTextSize(message, nullptr, false, wrap_width);
    const float height = text_size.y + padding * 2.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, with_alpha(color, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_Border, with_alpha(color, 0.42f));
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        tone == BadgeTone::Neutral ? palette.text_muted : palette.text
    );
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, metrics.small_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    ImGui::PushID(message);
    ImGui::BeginChild(
        "##modal_callout", ImVec2(0.0f, height), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
    ImGui::TextUnformatted(message);
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    ImGui::Spacing();
}

void modal_footer(float actions_width) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const float right_aligned = ImGui::GetWindowContentRegionMax().x - actions_width;
    if (right_aligned > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right_aligned);
}

} // namespace Engine::EditorUi
