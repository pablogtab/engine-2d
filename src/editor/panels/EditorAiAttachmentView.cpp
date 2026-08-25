#include "EditorAiAttachmentView.hpp"

#include "../../renderer/Texture.hpp"
#include "../ai/EditorAiAttachmentStore.hpp"

#include <imgui.h>

#include <algorithm>
#include <optional>

namespace Engine {

void draw_editor_ai_attachment_view(EditorAiAttachmentStore &attachments) {
  const auto &entities = attachments.entities();
  const auto &assets = attachments.assets();
  if (attachments.has_contexts()) {
    ImGui::BeginChild("AttachedEditorContexts", ImVec2(0.0f, 24.0f), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    std::optional<std::size_t> remove_entity;
    for (std::size_t index = 0; index < entities.size(); ++index) {
      if (index > 0)
        ImGui::SameLine();
      const auto &context = entities[index];
      ImGui::PushID(static_cast<int>(context.entity_id));
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_Header));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
      if (ImGui::SmallButton((context.name + "  x").c_str()))
        remove_entity = index;
      ImGui::PopStyleColor(2);
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Attached entity #%llu. Click to remove.",
                          static_cast<unsigned long long>(context.entity_id));
      }
      ImGui::PopID();
    }
    for (std::size_t index = 0; index < assets.size(); ++index) {
      if (!entities.empty() || index > 0)
        ImGui::SameLine();
      const auto &context = assets[index];
      ImGui::PushID(context.asset_handle.c_str());
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_Header));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
      const bool remove = ImGui::SmallButton((context.name + "  x").c_str());
      ImGui::PopStyleColor(2);
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Attached asset: %s. Click to remove.",
                          context.path.c_str());
      }
      ImGui::PopID();
      if (remove) {
        attachments.remove_asset(index);
        break;
      }
    }
    ImGui::EndChild();
    if (remove_entity)
      attachments.remove_entity(*remove_entity);
  }

  if (!attachments.has_screenshots())
    return;
  auto &screenshots = attachments.screenshots();
  ImGui::BeginChild("AttachedScreenshots", ImVec2(0.0f, 60.0f), false,
                    ImGuiWindowFlags_HorizontalScrollbar);
  std::optional<std::size_t> remove_screenshot;
  for (std::size_t index = 0; index < screenshots.size(); ++index) {
    if (index > 0)
      ImGui::SameLine();
    auto &screenshot = screenshots[index];
    ImGui::PushID(static_cast<int>(index));
    ImGui::BeginGroup();
    if (screenshot.preview && screenshot.preview->get_sdl_texture()) {
      const float width = static_cast<float>(screenshot.preview->get_width());
      const float height = static_cast<float>(screenshot.preview->get_height());
      const float scale = std::min(52.0f / width, 52.0f / height);
      ImGui::Image(screenshot.preview->get_sdl_texture(),
                   ImVec2(width * scale, height * scale));
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\n%d x %d px | %zu bytes", screenshot.name.c_str(),
                          screenshot.preview->get_width(),
                          screenshot.preview->get_height(),
                          screenshot.byte_count);
      }
      ImGui::SameLine();
    }
    if (ImGui::SmallButton("x"))
      remove_screenshot = index;
    ImGui::EndGroup();
    ImGui::PopID();
  }
  ImGui::EndChild();
  if (remove_screenshot)
    attachments.remove_screenshot(*remove_screenshot);
}

} // namespace Engine
