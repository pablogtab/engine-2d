#include "ParticleEffectAsset.hpp"
#include "../core/AssetDatabase.hpp"
#include "../core/FileSystem.hpp"
#include <cmath>
#include <filesystem>

namespace Engine {

namespace {

void set_error(std::string* error, const std::string& message) {
    if (error) *error = message;
}

bool parse_color(const nlohmann::json& value, SDL_Color& color) {
    if (!value.is_array() || value.size() != 4) return false;
    int channels[4]{};
    for (std::size_t index = 0; index < 4; ++index) {
        if (!value[index].is_number_integer()) return false;
        channels[index] = value[index].get<int>();
        if (channels[index] < 0 || channels[index] > 255) return false;
    }
    color = {
        static_cast<std::uint8_t>(channels[0]),
        static_cast<std::uint8_t>(channels[1]),
        static_cast<std::uint8_t>(channels[2]),
        static_cast<std::uint8_t>(channels[3])
    };
    return true;
}

} // namespace

nlohmann::json ParticleEffectAsset::to_json() const {
    nlohmann::json json;
    json["version"] = version;
    json["type"] = "ParticleEffect2D";
    json["name"] = name;
    if (texture_handle.is_valid()) {
        json["texture_handle"] = texture_handle.to_string();
    }

    json["appearance"] = {
        {"start_color", {appearance.start_color.r, appearance.start_color.g, appearance.start_color.b, appearance.start_color.a}},
        {"end_color", {appearance.end_color.r, appearance.end_color.g, appearance.end_color.b, appearance.end_color.a}},
        {"start_size", appearance.start_size},
        {"end_size", appearance.end_size},
        {"size_variation", appearance.size_variation},
        {"start_stretch", appearance.start_stretch},
        {"end_stretch", appearance.end_stretch}
    };

    json["motion"] = {
        {"initial_velocity", {motion.initial_velocity.x, motion.initial_velocity.y}},
        {"velocity_variation", {motion.velocity_variation.x, motion.velocity_variation.y}},
        {"acceleration", {motion.acceleration.x, motion.acceleration.y}},
        {"gravity", {motion.gravity.x, motion.gravity.y}},
        {"initial_rotation", motion.initial_rotation},
        {"rotation_variation", motion.rotation_variation},
        {"rotation_speed", motion.rotation_speed},
        {"rotation_speed_variation", motion.rotation_speed_variation},
        {"align_to_velocity", motion.align_to_velocity},
        {"velocity_alignment_offset", motion.velocity_alignment_offset}
    };

    json["lifetime"] = {
        {"duration", lifetime.duration},
        {"variation", lifetime.variation}
    };

    json["collision"] = {
        {"enabled", collision.enabled},
        {"mask_bits", collision.mask_bits},
        {"bounciness", collision.bounciness}
    };

    json["emission"] = {
        {"continuous_rate", emission.continuous_rate},
        {"distance_rate", emission.distance_rate},
        {"burst_count", emission.burst_count},
        {"duration", emission.duration},
        {"start_delay", emission.start_delay},
        {"loop", emission.loop}
    };

    json["rendering"] = {
        {"render_layer", static_cast<unsigned>(rendering.render_layer)},
        {"z_layer", rendering.z_layer},
        {"blend_mode", rendering.blend_mode == ParticleBlendMode::Additive ? "Additive" : "Alpha"}
    };

    return json;
}

std::optional<ParticleEffectAsset> ParticleEffectAsset::from_json(const nlohmann::json& json, std::string* error) {
    if (!json.is_object()) {
        set_error(error, "Particle effect JSON must be an object");
        return std::nullopt;
    }

    if (!json.contains("version") || !json.at("version").is_number_unsigned() ||
        json.at("version").get<uint32_t>() != CURRENT_VERSION) {
        set_error(error, "Particle effect uses an unsupported or missing schema version");
        return std::nullopt;
    }
    if (!json.contains("type") || !json.at("type").is_string() ||
        json.at("type").get<std::string>() != "ParticleEffect2D") {
        set_error(error, "Particle effect type must be 'ParticleEffect2D'");
        return std::nullopt;
    }
    if (!json.contains("name") || !json.at("name").is_string()) {
        set_error(error, "Particle effect name must be a string");
        return std::nullopt;
    }
    for (const char* section : {"appearance", "motion", "lifetime", "collision", "emission", "rendering"}) {
        if (!json.contains(section) || !json.at(section).is_object()) {
            set_error(error, std::string("Particle effect section '") + section + "' must be an object");
            return std::nullopt;
        }
    }

    ParticleEffectAsset asset;
    try {
    asset.version = json.at("version").get<uint32_t>();
    asset.name = json.at("name").get<std::string>();

    if (json.contains("texture_handle")) {
        if (!json.at("texture_handle").is_string()) {
            set_error(error, "Particle effect texture_handle must be a string");
            return std::nullopt;
        }
        const auto handle = AssetHandle::from_string(json.at("texture_handle").get<std::string>());
        if (!handle || !handle->is_valid()) {
            set_error(error, "Particle effect texture_handle is invalid");
            return std::nullopt;
        }
        asset.texture_handle = *handle;
    }

    if (json.contains("appearance") && json.at("appearance").is_object()) {
        const auto& app = json.at("appearance");
        if (app.contains("start_color") && !parse_color(app.at("start_color"), asset.appearance.start_color)) {
            set_error(error, "Particle effect start_color must contain four channels from 0 to 255");
            return std::nullopt;
        }
        if (app.contains("end_color") && !parse_color(app.at("end_color"), asset.appearance.end_color)) {
            set_error(error, "Particle effect end_color must contain four channels from 0 to 255");
            return std::nullopt;
        }
        asset.appearance.start_size = app.value("start_size", 10.0f);
        asset.appearance.end_size = app.value("end_size", 2.0f);
        asset.appearance.size_variation = app.value("size_variation", 0.0f);
        asset.appearance.start_stretch = app.value("start_stretch", 1.0f);
        asset.appearance.end_stretch = app.value("end_stretch", 1.0f);
    }

    if (json.contains("motion") && json.at("motion").is_object()) {
        const auto& mot = json.at("motion");
        if (mot.contains("initial_velocity") && mot.at("initial_velocity").is_array() && mot.at("initial_velocity").size() == 2) {
            asset.motion.initial_velocity = {mot.at("initial_velocity")[0].get<float>(), mot.at("initial_velocity")[1].get<float>()};
        }
        if (mot.contains("velocity_variation") && mot.at("velocity_variation").is_array() && mot.at("velocity_variation").size() == 2) {
            asset.motion.velocity_variation = {mot.at("velocity_variation")[0].get<float>(), mot.at("velocity_variation")[1].get<float>()};
        }
        if (mot.contains("acceleration") && mot.at("acceleration").is_array() && mot.at("acceleration").size() == 2) {
            asset.motion.acceleration = {mot.at("acceleration")[0].get<float>(), mot.at("acceleration")[1].get<float>()};
        }
        if (mot.contains("gravity") && mot.at("gravity").is_array() && mot.at("gravity").size() == 2) {
            asset.motion.gravity = {mot.at("gravity")[0].get<float>(), mot.at("gravity")[1].get<float>()};
        }
        asset.motion.initial_rotation = mot.value("initial_rotation", 0.0f);
        asset.motion.rotation_variation = mot.value("rotation_variation", 0.0f);
        asset.motion.rotation_speed = mot.value("rotation_speed", 0.0f);
        asset.motion.rotation_speed_variation = mot.value("rotation_speed_variation", 0.0f);
        asset.motion.align_to_velocity = mot.value("align_to_velocity", false);
        asset.motion.velocity_alignment_offset = mot.value("velocity_alignment_offset", 0.0f);
    }

    if (json.contains("lifetime") && json.at("lifetime").is_object()) {
        const auto& lt = json.at("lifetime");
        asset.lifetime.duration = lt.value("duration", 1.0f);
        asset.lifetime.variation = lt.value("variation", 0.2f);
    }

    if (json.contains("collision") && json.at("collision").is_object()) {
        const auto& col = json.at("collision");
        asset.collision.enabled = col.value("enabled", false);
        asset.collision.mask_bits = col.value("mask_bits", static_cast<uint16_t>(CollisionCategory::GROUND));
        asset.collision.bounciness = col.value("bounciness", 0.5f);
    }

    if (json.contains("emission") && json.at("emission").is_object()) {
        const auto& emi = json.at("emission");
        asset.emission.continuous_rate = emi.value("continuous_rate", 20.0f);
        asset.emission.distance_rate = emi.value("distance_rate", 0.0f);
        asset.emission.burst_count = emi.value("burst_count", 10);
        asset.emission.duration = emi.value("duration", 0.0f);
        asset.emission.start_delay = emi.value("start_delay", 0.0f);
        asset.emission.loop = emi.value("loop", true);
    }

    if (json.contains("rendering") && json.at("rendering").is_object()) {
        const auto& ren = json.at("rendering");
        const std::string blend_mode = ren.value("blend_mode", std::string{"Alpha"});
        if (blend_mode != "Alpha" && blend_mode != "Additive") {
            set_error(error, "Particle effect blend_mode must be 'Alpha' or 'Additive'");
            return std::nullopt;
        }
        asset.rendering.render_layer = static_cast<RenderLayer>(ren.value("render_layer", 0u));
        asset.rendering.z_layer = ren.value("z_layer", -0.05f);
        asset.rendering.blend_mode = blend_mode == "Additive"
            ? ParticleBlendMode::Additive : ParticleBlendMode::Alpha;
    }

    } catch (const nlohmann::json::exception& exception) {
        set_error(error, std::string("Particle effect contains an invalid field: ") + exception.what());
        return std::nullopt;
    }

    if (!asset.validate(error)) return std::nullopt;
    return asset;
}

bool ParticleEffectAsset::validate(std::string* error) const {
    const auto finite = [](float value) { return std::isfinite(value); };
    if (version != CURRENT_VERSION) {
        set_error(error, "Particle effect schema version is unsupported");
        return false;
    }
    if (name.empty()) {
        set_error(error, "Particle effect name cannot be empty");
        return false;
    }
    if (!finite(appearance.start_size) || !finite(appearance.end_size) ||
        !finite(appearance.size_variation) || !finite(appearance.start_stretch) ||
        !finite(appearance.end_stretch) || appearance.start_size < 0.0f ||
        appearance.end_size < 0.0f || appearance.size_variation < 0.0f) {
        set_error(error, "Particle sizes, size variation and stretch must be finite and non-negative");
        return false;
    }
    if (!finite(lifetime.duration) || !finite(lifetime.variation) ||
        lifetime.duration <= 0.0f || lifetime.variation < 0.0f) {
        set_error(error, "Particle lifetime must be positive and variation must be non-negative");
        return false;
    }
    if (!finite(collision.bounciness) || collision.bounciness < 0.0f || collision.bounciness > 1.0f) {
        set_error(error, "Particle bounciness must be between 0 and 1");
        return false;
    }
    if (!finite(emission.continuous_rate) || !finite(emission.distance_rate) || !finite(emission.duration) ||
        !finite(emission.start_delay) || emission.continuous_rate < 0.0f ||
        emission.distance_rate < 0.0f ||
        emission.burst_count < 0 || emission.duration < 0.0f || emission.start_delay < 0.0f) {
        set_error(error, "Particle emission values must be finite and non-negative");
        return false;
    }
    if (rendering.render_layer > 31 || !finite(rendering.z_layer)) {
        set_error(error, "Particle rendering layer must be between 0 and 31 and depth must be finite");
        return false;
    }
    const glm::vec2 vectors[] = {
        motion.initial_velocity, motion.velocity_variation, motion.acceleration, motion.gravity
    };
    for (const glm::vec2& value : vectors) {
        if (!finite(value.x) || !finite(value.y)) {
            set_error(error, "Particle motion vectors must contain finite values");
            return false;
        }
    }
    if (!finite(motion.initial_rotation) || !finite(motion.rotation_variation) ||
        !finite(motion.rotation_speed) || !finite(motion.rotation_speed_variation) ||
        !finite(motion.velocity_alignment_offset) ||
        motion.velocity_variation.x < 0.0f || motion.velocity_variation.y < 0.0f ||
        motion.rotation_variation < 0.0f || motion.rotation_speed_variation < 0.0f) {
        set_error(error, "Particle variation values must be non-negative");
        return false;
    }
    return true;
}

std::optional<ParticleEffectAsset> ParticleEffectAsset::load_asset(AssetHandle handle, const AssetDatabase& database) {
    const AssetRecord* record = database.find(handle);
    if (!record || record->status != AssetStatus::Ready || record->type != AssetType::ParticleEffect) {
        return std::nullopt;
    }
    const std::string full_path = (database.project_root() / record->source_path).string();
    return load_from_file(full_path);
}

std::optional<ParticleEffectAsset> ParticleEffectAsset::load_from_file(const std::string& path, std::string* error) {
    const std::string text = FileSystem::read_text(path);
    if (text.empty()) {
        set_error(error, "Failed to read particle effect file or file is empty: " + path);
        return std::nullopt;
    }
    const auto json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded()) {
        set_error(error, "Failed parsing particle effect JSON: " + path);
        return std::nullopt;
    }
    return from_json(json, error);
}

bool ParticleEffectAsset::save_asset(const std::string& path, AssetDatabase& database, std::string* error) const {
    if (!validate(error)) return false;
    if (texture_handle.is_valid()) {
        const AssetRecord* texture = database.find(texture_handle);
        if (!texture || texture->status != AssetStatus::Ready || texture->type != AssetType::Texture) {
            set_error(error, "Particle effect texture_handle must reference a ready texture asset");
            return false;
        }
    }
    if (std::filesystem::path(path).extension() != ".particle") {
        set_error(error, "Particle effect assets must have .particle extension");
        return false;
    }
    const std::string json_str = to_json().dump(2);
    const bool existed = std::filesystem::is_regular_file(path);
    const std::string rollback = existed ? FileSystem::read_text(path) : std::string{};
    if (!FileSystem::write_text_atomically(path, json_str)) {
        set_error(error, "Failed writing particle effect asset atomically to: " + path);
        return false;
    }
    const auto handle = database.import_asset(path);
    if (handle && database.find(*handle) && database.find(*handle)->type == AssetType::ParticleEffect) return true;
    if (existed) {
        static_cast<void>(FileSystem::write_text_atomically(path, rollback));
        static_cast<void>(database.import_asset(path));
    } else {
        std::error_code remove_error;
        std::filesystem::remove(path, remove_error);
        static_cast<void>(database.remove_asset(path));
    }
    set_error(error, "Failed importing particle effect asset into the project database");
    return false;
}

} // namespace Engine
