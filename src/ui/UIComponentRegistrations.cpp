#include "UIComponentRegistrations.hpp"

#include "../ecs/components/UIComponents.hpp"
#include "../renderer/material/MaterialAsset.hpp"
#include "../scene/ComponentRegistry.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace Engine {
namespace {

using Json = nlohmann::json;

template<typename Component>
void register_or_throw(ComponentRegistry& registry, TypedComponentRegistration<Component> registration) {
    std::string error;
    if (!registry.register_component<Component>(std::move(registration), &error)) {
        throw std::runtime_error(error);
    }
}

Json color_to_json(const SDL_Color& color) {
    return Json::array({color.r, color.g, color.b, color.a});
}

bool color_from_json(const Json& value, SDL_Color& color, std::string& error) {
    if (!value.is_array() || value.size() != 4) {
        error = "UI color must contain four channels";
        return false;
    }
    for (const auto& channel : value) {
        if (!channel.is_number_unsigned() && !channel.is_number_integer()) {
            error = "UI color channels must be integers";
            return false;
        }
        const int numeric = channel.get<int>();
        if (numeric < 0 || numeric > 255) {
            error = "UI color channels must be between 0 and 255";
            return false;
        }
    }
    color = {
        value[0].get<std::uint8_t>(), value[1].get<std::uint8_t>(),
        value[2].get<std::uint8_t>(), value[3].get<std::uint8_t>()
    };
    return true;
}

Json vec2_to_json(const glm::vec2& value) {
    return Json::array({value.x, value.y});
}

Json vec4_to_json(const glm::vec4& value) {
    return Json::array({value.x, value.y, value.z, value.w});
}

bool vec2_from_json(const Json& value, glm::vec2& result, const char* label, std::string& error) {
    if (!value.is_array() || value.size() != 2 ||
        !value[0].is_number() || !value[1].is_number()) {
        error = std::string(label) + " must contain two numeric values";
        return false;
    }
    result = {value[0].get<float>(), value[1].get<float>()};
    return true;
}

bool vec4_from_json(const Json& value, glm::vec4& result, const char* label, std::string& error) {
    if (!value.is_array() || value.size() != 4 ||
        !value[0].is_number() || !value[1].is_number() ||
        !value[2].is_number() || !value[3].is_number()) {
        error = std::string(label) + " must contain four numeric values";
        return false;
    }
    result = {
        value[0].get<float>(), value[1].get<float>(),
        value[2].get<float>(), value[3].get<float>()
    };
    return true;
}

const char* presentation_to_string(PresentationMode mode) {
    switch (mode) {
        case PresentationMode::Fit: return "fit";
        case PresentationMode::Fill: return "fill";
        case PresentationMode::Stretch: return "stretch";
        case PresentationMode::Responsive: return "responsive";
    }
    return "responsive";
}

bool presentation_from_string(std::string_view value, PresentationMode& result) {
    if (value == "fit") result = PresentationMode::Fit;
    else if (value == "fill") result = PresentationMode::Fill;
    else if (value == "stretch") result = PresentationMode::Stretch;
    else if (value == "responsive") result = PresentationMode::Responsive;
    else return false;
    return true;
}

const char* horizontal_alignment_to_string(UIHorizontalAlignment alignment) {
    switch (alignment) {
        case UIHorizontalAlignment::Left: return "left";
        case UIHorizontalAlignment::Center: return "center";
        case UIHorizontalAlignment::Right: return "right";
    }
    return "center";
}

bool horizontal_alignment_from_string(std::string_view value, UIHorizontalAlignment& result) {
    if (value == "left") result = UIHorizontalAlignment::Left;
    else if (value == "center") result = UIHorizontalAlignment::Center;
    else if (value == "right") result = UIHorizontalAlignment::Right;
    else return false;
    return true;
}

const char* vertical_alignment_to_string(UIVerticalAlignment alignment) {
    switch (alignment) {
        case UIVerticalAlignment::Top: return "top";
        case UIVerticalAlignment::Center: return "center";
        case UIVerticalAlignment::Bottom: return "bottom";
    }
    return "center";
}

bool vertical_alignment_from_string(std::string_view value, UIVerticalAlignment& result) {
    if (value == "top") result = UIVerticalAlignment::Top;
    else if (value == "center") result = UIVerticalAlignment::Center;
    else if (value == "bottom") result = UIVerticalAlignment::Bottom;
    else return false;
    return true;
}

const char* image_scale_to_string(UIImageScaleMode mode) {
    switch (mode) {
        case UIImageScaleMode::Stretch: return "stretch";
        case UIImageScaleMode::Fit: return "fit";
        case UIImageScaleMode::Fill: return "fill";
    }
    return "stretch";
}

bool image_scale_from_string(std::string_view value, UIImageScaleMode& result) {
    if (value == "stretch") result = UIImageScaleMode::Stretch;
    else if (value == "fit") result = UIImageScaleMode::Fit;
    else if (value == "fill") result = UIImageScaleMode::Fill;
    else return false;
    return true;
}

Json optional_handle_to_json(AssetHandle handle) {
    return handle.is_valid() ? Json(handle.to_string()) : Json(nullptr);
}

bool optional_handle_from_json(
    const Json& value,
    AssetHandle& handle,
    const char* label,
    std::string& error
) {
    if (value.is_null()) {
        handle = {};
        return true;
    }
    if (!value.is_string()) {
        error = std::string(label) + " must be an asset handle or null";
        return false;
    }
    const auto parsed = AssetHandle::from_string(value.get<std::string>());
    if (!parsed || !parsed->is_valid()) {
        error = std::string(label) + " is invalid";
        return false;
    }
    handle = *parsed;
    return true;
}

bool finite_vec2(const glm::vec2& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite_vec4(const glm::vec4& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z) && std::isfinite(value.w);
}

const char* length_unit_to_string(UILengthUnit unit) {
    return unit == UILengthUnit::Percent ? "percent" : "px";
}

bool length_unit_from_string(std::string_view value, UILengthUnit& result) {
    if (value == "px") result = UILengthUnit::Pixels;
    else if (value == "percent") result = UILengthUnit::Percent;
    else return false;
    return true;
}

Json length_to_json(const UILength& length) {
    return Json{{"value", length.value}, {"unit", length_unit_to_string(length.unit)}};
}

bool length_from_json(const Json& value, UILength& result, const char* label, std::string& error) {
    if (!value.is_object() || !value.contains("value") || !value.at("value").is_number() ||
        !value.contains("unit") || !value.at("unit").is_string()) {
        error = std::string(label) + " must contain numeric value and unit";
        return false;
    }
    result.value = value.at("value").get<float>();
    if (!length_unit_from_string(value.at("unit").get<std::string>(), result.unit)) {
        error = std::string(label) + " unit must be px or percent";
        return false;
    }
    return true;
}

Json edge_lengths_to_json(const UIEdgeLengths& edges) {
    return Json{
        {"left", length_to_json(edges.left)},
        {"top", length_to_json(edges.top)},
        {"right", length_to_json(edges.right)},
        {"bottom", length_to_json(edges.bottom)}
    };
}

bool edge_lengths_from_json(
    const Json& value,
    UIEdgeLengths& result,
    const char* label,
    std::string& error
) {
    if (!value.is_object()) {
        error = std::string(label) + " must be an object";
        return false;
    }
    return length_from_json(value.at("left"), result.left, "UI Text left margin", error) &&
        length_from_json(value.at("top"), result.top, "UI Text top margin", error) &&
        length_from_json(value.at("right"), result.right, "UI Text right margin", error) &&
        length_from_json(value.at("bottom"), result.bottom, "UI Text bottom margin", error);
}

bool finite_edges(const UIEdgeLengths& edges) {
    return std::isfinite(edges.left.value) && std::isfinite(edges.top.value) &&
        std::isfinite(edges.right.value) && std::isfinite(edges.bottom.value);
}

float percentage_fraction(const UILength& length) {
    return length.unit == UILengthUnit::Percent ? length.value * 0.01f : 0.0f;
}

} // namespace

void register_ui_components(ComponentRegistry& registry) {
    register_or_throw<CanvasComponent>(registry, {
        .display_name = "Canvas",
        .serialized_key = "canvas",
        .serialize = [](const CanvasComponent& canvas, const ComponentSerializationContext&) {
            return Json{
                {"reference_size", vec2_to_json(canvas.reference_size)},
                {"scaling_policy", presentation_to_string(canvas.scaling_policy)},
                {"sorting_order", canvas.sorting_order},
                {"use_safe_area", canvas.use_safe_area},
                {"enabled", canvas.enabled}
            };
        },
        .deserialize = [](CanvasComponent& canvas, const Json& value, const ComponentSerializationContext&, std::string& error) {
            if (!value.is_object()) { error = "Canvas must be an object"; return false; }
            if (!vec2_from_json(value.at("reference_size"), canvas.reference_size, "Canvas reference_size", error)) return false;
            if (!value.at("scaling_policy").is_string() ||
                !presentation_from_string(value.at("scaling_policy").get<std::string>(), canvas.scaling_policy)) {
                error = "Canvas scaling_policy is invalid";
                return false;
            }
            canvas.sorting_order = value.at("sorting_order").get<std::int16_t>();
            canvas.use_safe_area = value.at("use_safe_area").get<bool>();
            canvas.enabled = value.at("enabled").get<bool>();
            return true;
        },
        .validate = [](const CanvasComponent& canvas) -> std::optional<std::string> {
            if (!finite_vec2(canvas.reference_size) ||
                canvas.reference_size.x <= 0.0f || canvas.reference_size.y <= 0.0f) {
                return "Canvas reference size must be finite and positive";
            }
            return std::nullopt;
        }
    });

    register_or_throw<UILayoutComponent>(registry, {
        .display_name = "UI Layout",
        .serialized_key = "ui_layout",
        .serialize = [](const UILayoutComponent& layout, const ComponentSerializationContext&) {
            return Json{
                {"anchor_min", vec2_to_json(layout.anchor_min)},
                {"anchor_max", vec2_to_json(layout.anchor_max)},
                {"pivot", vec2_to_json(layout.pivot)},
                {"anchored_position", vec2_to_json(layout.anchored_position)},
                {"size_delta", vec2_to_json(layout.size_delta)}
            };
        },
        .deserialize = [](UILayoutComponent& layout, const Json& value, const ComponentSerializationContext&, std::string& error) {
            if (!value.is_object()) { error = "UI Layout must be an object"; return false; }
            return vec2_from_json(value.at("anchor_min"), layout.anchor_min, "UI Layout anchor_min", error) &&
                vec2_from_json(value.at("anchor_max"), layout.anchor_max, "UI Layout anchor_max", error) &&
                vec2_from_json(value.at("pivot"), layout.pivot, "UI Layout pivot", error) &&
                vec2_from_json(value.at("anchored_position"), layout.anchored_position, "UI Layout anchored_position", error) &&
                vec2_from_json(value.at("size_delta"), layout.size_delta, "UI Layout size_delta", error);
        },
        .validate = [](const UILayoutComponent& layout) -> std::optional<std::string> {
            if (!finite_vec2(layout.anchor_min) || !finite_vec2(layout.anchor_max) ||
                !finite_vec2(layout.pivot) || !finite_vec2(layout.anchored_position) ||
                !finite_vec2(layout.size_delta)) {
                return "UI Layout values must be finite";
            }
            if (glm::any(glm::lessThan(layout.anchor_min, glm::vec2(0.0f))) ||
                glm::any(glm::greaterThan(layout.anchor_max, glm::vec2(1.0f))) ||
                glm::any(glm::greaterThan(layout.anchor_min, layout.anchor_max))) {
                return "UI Layout anchors must be ordered between 0 and 1";
            }
            if (glm::any(glm::lessThan(layout.pivot, glm::vec2(0.0f))) ||
                glm::any(glm::greaterThan(layout.pivot, glm::vec2(1.0f)))) {
                return "UI Layout pivot must be between 0 and 1";
            }
            return std::nullopt;
        }
    });

    register_or_throw<UITextComponent>(registry, {
        .display_name = "UI Text",
        .serialized_key = "ui_text",
        .serialize = [](const UITextComponent& text, const ComponentSerializationContext&) {
            return Json{
                {"text", text.text},
                {"font_handle", optional_handle_to_json(text.font_handle)},
                {"material_handle", optional_handle_to_json(text.material_handle)},
                {"font_size", text.font_size},
                {"color", color_to_json(text.color)},
                {"horizontal_alignment", horizontal_alignment_to_string(text.horizontal_alignment)},
                {"vertical_alignment", vertical_alignment_to_string(text.vertical_alignment)},
                {"margins", edge_lengths_to_json(text.margins)},
                {"fit_content_width", text.fit_content_width},
                {"fit_content_height", text.fit_content_height},
                {"fit_padding", vec4_to_json(text.fit_padding)},
                {"wrap", text.wrap},
                {"enabled", text.enabled}
            };
        },
        .deserialize = [](UITextComponent& text, const Json& value, const ComponentSerializationContext& context, std::string& error) {
            if (!value.is_object()) { error = "UI Text must be an object"; return false; }
            if (!value.at("text").is_string()) { error = "UI Text text must be a string"; return false; }
            text.text = value.at("text").get<std::string>();
            if (!optional_handle_from_json(value.at("font_handle"), text.font_handle, "UI Text font_handle", error)) return false;
            if (!optional_handle_from_json(value.at("material_handle"), text.material_handle, "UI Text material_handle", error)) return false;
            text.material = text.material_handle.is_valid() && context.asset_database
                ? MaterialAsset::load(text.material_handle, *context.asset_database)
                : nullptr;
            text.font_size = value.at("font_size").get<float>();
            if (!color_from_json(value.at("color"), text.color, error)) return false;
            if (!value.at("horizontal_alignment").is_string() ||
                !horizontal_alignment_from_string(value.at("horizontal_alignment").get<std::string>(), text.horizontal_alignment)) {
                error = "UI Text horizontal_alignment is invalid";
                return false;
            }
            if (!value.at("vertical_alignment").is_string() ||
                !vertical_alignment_from_string(value.at("vertical_alignment").get<std::string>(), text.vertical_alignment)) {
                error = "UI Text vertical_alignment is invalid";
                return false;
            }
            if (!edge_lengths_from_json(value.at("margins"), text.margins, "UI Text margins", error)) return false;
            text.fit_content_width = value.at("fit_content_width").get<bool>();
            text.fit_content_height = value.at("fit_content_height").get<bool>();
            if (!vec4_from_json(value.at("fit_padding"), text.fit_padding, "UI Text fit_padding", error)) return false;
            text.wrap = value.at("wrap").get<bool>();
            text.enabled = value.at("enabled").get<bool>();
            return true;
        },
        .validate = [](const UITextComponent& text) -> std::optional<std::string> {
            if (!std::isfinite(text.font_size) || text.font_size <= 0.0f || text.font_size > 512.0f) {
                return "UI Text font size must be between 0 and 512";
            }
            if (!finite_edges(text.margins)) {
                return "UI Text margins must be finite";
            }
            if (!finite_vec4(text.fit_padding) ||
                glm::any(glm::lessThan(text.fit_padding, glm::vec4(0.0f)))) {
                return "UI Text fit padding must be finite and non-negative";
            }
            if (text.fit_content_width &&
                percentage_fraction(text.margins.left) +
                    percentage_fraction(text.margins.right) >= 1.0f) {
                return "UI Text horizontal percentage margins must total less than 100 when fitting width";
            }
            if (text.fit_content_height &&
                percentage_fraction(text.margins.top) +
                    percentage_fraction(text.margins.bottom) >= 1.0f) {
                return "UI Text vertical percentage margins must total less than 100 when fitting height";
            }
            return std::nullopt;
        }
    });

    register_or_throw<UIImageComponent>(registry, {
        .display_name = "UI Image",
        .serialized_key = "ui_image",
        .serialize = [](const UIImageComponent& image, const ComponentSerializationContext&) {
            return Json{
                {"texture_handle", optional_handle_to_json(image.texture_handle)},
                {"material_handle", optional_handle_to_json(image.material_handle)},
                {"color", color_to_json(image.color)},
                {"scale_mode", image_scale_to_string(image.scale_mode)},
                {"raycast_target", image.raycast_target},
                {"enabled", image.enabled}
            };
        },
        .deserialize = [](UIImageComponent& image, const Json& value, const ComponentSerializationContext& context, std::string& error) {
            if (!value.is_object()) { error = "UI Image must be an object"; return false; }
            if (!optional_handle_from_json(value.at("texture_handle"), image.texture_handle, "UI Image texture_handle", error)) return false;
            if (!optional_handle_from_json(value.at("material_handle"), image.material_handle, "UI Image material_handle", error)) return false;
            image.material = image.material_handle.is_valid() && context.asset_database
                ? MaterialAsset::load(image.material_handle, *context.asset_database)
                : nullptr;
            if (!color_from_json(value.at("color"), image.color, error)) return false;
            if (!value.at("scale_mode").is_string() ||
                !image_scale_from_string(value.at("scale_mode").get<std::string>(), image.scale_mode)) {
                error = "UI Image scale_mode is invalid";
                return false;
            }
            image.raycast_target = value.at("raycast_target").get<bool>();
            image.enabled = value.at("enabled").get<bool>();
            return true;
        }
    });

    register_or_throw<UIButtonComponent>(registry, {
        .display_name = "UI Button",
        .serialized_key = "ui_button",
        .serialize = [](const UIButtonComponent& button, const ComponentSerializationContext&) {
            return Json{
                {"normal_color", color_to_json(button.normal_color)},
                {"hovered_color", color_to_json(button.hovered_color)},
                {"pressed_color", color_to_json(button.pressed_color)},
                {"disabled_color", color_to_json(button.disabled_color)},
                {"interactable", button.interactable},
                {"enabled", button.enabled}
            };
        },
        .deserialize = [](UIButtonComponent& button, const Json& value, const ComponentSerializationContext&, std::string& error) {
            if (!value.is_object()) { error = "UI Button must be an object"; return false; }
            if (!color_from_json(value.at("normal_color"), button.normal_color, error) ||
                !color_from_json(value.at("hovered_color"), button.hovered_color, error) ||
                !color_from_json(value.at("pressed_color"), button.pressed_color, error) ||
                !color_from_json(value.at("disabled_color"), button.disabled_color, error)) return false;
            button.interactable = value.at("interactable").get<bool>();
            button.enabled = value.at("enabled").get<bool>();
            return true;
        }
    });
}

} // namespace Engine
