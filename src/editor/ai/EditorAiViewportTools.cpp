#include "EditorAiViewportTools.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Engine {
namespace {

nlohmann::json empty_object_schema() {
    return {
        {"type", "object"},
        {"properties", nlohmann::json::object()},
        {"required", nlohmann::json::array()},
        {"additionalProperties", false}
    };
}

bool only_keys(const nlohmann::json& value, std::initializer_list<const char*> keys) {
    if (!value.is_object()) return false;
    for (auto item = value.begin(); item != value.end(); ++item) {
        if (std::none_of(keys.begin(), keys.end(), [&item](const char* key) {
                return item.key() == key;
            })) {
            return false;
        }
    }
    return true;
}

bool finite_number(const nlohmann::json& value, const char* key, float& output) {
    if (!value.contains(key) || !value.at(key).is_number()) return false;
    const double parsed = value.at(key).get<double>();
    if (!std::isfinite(parsed) || parsed < -1000000.0 || parsed > 1000000.0) return false;
    output = static_cast<float>(parsed);
    return true;
}

} // namespace

EditorAiViewportTools::EditorAiViewportTools()
    : m_tool_schemas(nlohmann::json::array({
          {
              {"type", "function"},
              {"name", "get_viewport_state"},
              {"description", "Returns the live editor viewport camera position, zoom, visible world bounds, render size, and render generation."},
              {"parameters", empty_object_schema()},
              {"strict", true}
          },
          {
              {"type", "function"},
              {"name", "pan_viewport"},
              {"description", "Moves the editor viewport in world units without modifying the scene. Positive X moves right and positive Y moves up."},
              {"parameters", {
                  {"type", "object"},
                  {"properties", {
                      {"delta_x", {{"type", "number"}, {"minimum", -1000000.0}, {"maximum", 1000000.0}}},
                      {"delta_y", {{"type", "number"}, {"minimum", -1000000.0}, {"maximum", 1000000.0}}}
                  }},
                  {"required", {"delta_x", "delta_y"}},
                  {"additionalProperties", false}
              }},
              {"strict", true}
          },
          {
              {"type", "function"},
              {"name", "zoom_viewport"},
              {"description", "Multiplies the editor viewport zoom around its center. Values above 1 zoom in; values below 1 zoom out."},
              {"parameters", {
                  {"type", "object"},
                  {"properties", {
                      {"factor", {{"type", "number"}, {"minimum", 0.25}, {"maximum", 4.0}}}
                  }},
                  {"required", {"factor"}},
                  {"additionalProperties", false}
              }},
              {"strict", true}
          },
          {
              {"type", "function"},
              {"name", "focus_viewport_entities"},
              {"description", "Frames one or more existing numeric entity IDs in the editor viewport. Resolve entity IDs before calling."},
              {"parameters", {
                  {"type", "object"},
                  {"properties", {
                      {"entity_ids", {
                          {"type", "array"},
                          {"items", {{"type", "integer"}, {"minimum", 1}}},
                          {"minItems", 1},
                          {"maxItems", 20},
                          {"uniqueItems", true}
                      }},
                      {"padding", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}}
                  }},
                  {"required", {"entity_ids", "padding"}},
                  {"additionalProperties", false}
              }},
              {"strict", true}
          },
          {
              {"type", "function"},
              {"name", "frame_all_viewport"},
              {"description", "Frames all visible scene entities in the editor viewport without modifying authored content."},
              {"parameters", {
                  {"type", "object"},
                  {"properties", {
                      {"padding", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}}
                  }},
                  {"required", {"padding"}},
                  {"additionalProperties", false}
              }},
              {"strict", true}
          },
          {
              {"type", "function"},
              {"name", "reset_viewport"},
              {"description", "Resets the editor viewport camera to world origin and 1x zoom."},
              {"parameters", empty_object_schema()},
              {"strict", true}
          },
          {
              {"type", "function"},
              {"name", "capture_viewport"},
              {"description", "Captures the current rendered editor viewport and returns it as visual tool output with compact camera metadata. Use after navigation to inspect the scene."},
              {"parameters", empty_object_schema()},
              {"strict", true}
          },
          {
              {"type", "function"},
              {"name", "get_scene_diagnostics"},
              {"description", "Runs the live scene validator and returns bounded errors and warnings without changing the scene."},
              {"parameters", {
                  {"type", "object"},
                  {"properties", {
                      {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}
                  }},
                  {"required", {"limit"}},
                  {"additionalProperties", false}
              }},
              {"strict", true}
          }
      })) {}

bool EditorAiViewportTools::is_tool(const std::string& tool_name) const {
    return std::any_of(m_tool_schemas.begin(), m_tool_schemas.end(), [&](const auto& schema) {
        return schema.value("name", "") == tool_name;
    });
}

bool EditorAiViewportTools::parse(
    const std::string& tool_name,
    const nlohmann::json& arguments,
    EditorAiViewportToolCommand& command,
    std::string& error
) const {
    error.clear();
    command = {};
    if (!arguments.is_object()) {
        error = "Viewport tool arguments must be an object";
        return false;
    }

    if (tool_name == "get_viewport_state" || tool_name == "reset_viewport" ||
        tool_name == "capture_viewport") {
        if (!arguments.empty()) {
            error = tool_name + " does not accept arguments";
            return false;
        }
        command.type = tool_name == "get_viewport_state"
            ? EditorAiViewportToolType::GetState
            : (tool_name == "reset_viewport"
                ? EditorAiViewportToolType::Reset
                : EditorAiViewportToolType::Capture);
        return true;
    }

    if (tool_name == "pan_viewport") {
        if (!only_keys(arguments, {"delta_x", "delta_y"}) || arguments.size() != 2 ||
            !finite_number(arguments, "delta_x", command.x) ||
            !finite_number(arguments, "delta_y", command.y)) {
            error = "pan_viewport requires finite delta_x and delta_y values";
            return false;
        }
        command.type = EditorAiViewportToolType::Pan;
        return true;
    }

    if (tool_name == "zoom_viewport") {
        if (!only_keys(arguments, {"factor"}) || arguments.size() != 1 ||
            !finite_number(arguments, "factor", command.factor) ||
            command.factor < 0.25f || command.factor > 4.0f) {
            error = "zoom_viewport requires a factor between 0.25 and 4";
            return false;
        }
        command.type = EditorAiViewportToolType::Zoom;
        return true;
    }

    if (tool_name == "focus_viewport_entities") {
        if (!only_keys(arguments, {"entity_ids", "padding"}) ||
            !arguments.contains("entity_ids") || !arguments.at("entity_ids").is_array() ||
            arguments.at("entity_ids").empty() || arguments.at("entity_ids").size() > 20) {
            error = "focus_viewport_entities requires 1-20 unique entity_ids and padding from 0 to 1";
            return false;
        }
        if (arguments.contains("padding") && !arguments.at("padding").is_null() &&
            (!finite_number(arguments, "padding", command.padding) ||
             command.padding < 0.0f || command.padding > 1.0f)) {
            error = "focus_viewport_entities requires padding from 0 to 1 when provided";
            return false;
        }
        std::unordered_set<EntityID> unique;
        for (const auto& value : arguments.at("entity_ids")) {
            if (!value.is_number_unsigned() && !value.is_number_integer()) {
                error = "Viewport entity IDs must be positive integers";
                return false;
            }
            const std::int64_t parsed = value.get<std::int64_t>();
            if (parsed <= 0 || static_cast<std::uint64_t>(parsed) >
                    static_cast<std::uint64_t>(std::numeric_limits<EntityID>::max())) {
                error = "Viewport entity ID is out of range";
                return false;
            }
            const EntityID entity = static_cast<EntityID>(parsed);
            if (!unique.insert(entity).second) {
                error = "Viewport entity IDs must be unique";
                return false;
            }
            command.entity_ids.push_back(entity);
        }
        command.type = EditorAiViewportToolType::FocusEntities;
        return true;
    }

    if (tool_name == "frame_all_viewport") {
        if (!only_keys(arguments, {"padding"}) ||
            (arguments.contains("padding") && !arguments.at("padding").is_null() &&
             (!finite_number(arguments, "padding", command.padding) ||
              command.padding < 0.0f || command.padding > 1.0f))) {
            error = "frame_all_viewport requires padding from 0 to 1 when provided";
            return false;
        }
        command.type = EditorAiViewportToolType::FrameAll;
        return true;
    }

    if (tool_name == "get_scene_diagnostics") {
        if (!only_keys(arguments, {"limit"})) {
            error = "get_scene_diagnostics accepts only an integer limit";
            return false;
        }
        if (!arguments.contains("limit") || arguments.at("limit").is_null()) {
            command.type = EditorAiViewportToolType::GetDiagnostics;
            return true;
        }
        if (!arguments.at("limit").is_number_integer()) {
            error = "get_scene_diagnostics requires an integer limit when provided";
            return false;
        }
        const std::int64_t limit = arguments.at("limit").get<std::int64_t>();
        if (limit < 1 || limit > 100) {
            error = "Diagnostic limit must be between 1 and 100";
            return false;
        }
        command.type = EditorAiViewportToolType::GetDiagnostics;
        command.limit = static_cast<std::size_t>(limit);
        return true;
    }

    error = "Unknown viewport tool: " + tool_name;
    return false;
}

} // namespace Engine
