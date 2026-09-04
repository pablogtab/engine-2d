#include "VfxPreviewPanel.hpp"

#include "../EditorTheme.hpp"
#include "../ui/EditorUi.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/AssetManager.hpp"
#include "../../core/FileSystem.hpp"
#include "../../ecs/RenderSystem.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../ecs/components/AnimationComponents.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/ParticleComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../ecs/components/ScriptingComponents.hpp"
#include "../../renderer/Renderer2D.hpp"
#include "../../renderer/SpriteAnimationSystem.hpp"
#include "../../renderer/material/MaterialAsset.hpp"
#include "../../renderer/material/ShaderAsset.hpp"
#include "../../scene/ComponentRegistry.hpp"
#include "../../scene/EntitySubtreeArchive.hpp"
#include "../../scene/PrefabSerializer.hpp"
#include "../../scene/Scene.hpp"
#include "../../scene/SceneManager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <limits>
#include <array>
#include <cstring>
#include <cstdio>
#include <string_view>
#include <unordered_set>

namespace Engine {
namespace {

float color_channel(std::uint8_t channel) {
    return static_cast<float>(channel) / 255.0f;
}

std::uint8_t color_byte(float channel) {
    return static_cast<std::uint8_t>(
        std::clamp(channel, 0.0f, 1.0f) * 255.0f + 0.5f
    );
}

std::string parameter_display_name(std::string_view name) {
    std::string result;
    result.reserve(name.size() + 4);
    for (std::size_t index = 0; index < name.size(); ++index) {
        const unsigned char current = static_cast<unsigned char>(name[index]);
        if (current == '_') {
            if (!result.empty() && result.back() != ' ') result.push_back(' ');
            continue;
        }
        const bool upper_boundary = index > 0 && std::isupper(current) &&
            (std::islower(static_cast<unsigned char>(name[index - 1])) ||
             (index + 1 < name.size() &&
              std::islower(static_cast<unsigned char>(name[index + 1]))));
        if (upper_boundary && !result.empty() && result.back() != ' ') result.push_back(' ');
        result.push_back(static_cast<char>(current));
    }
    if (!result.empty()) {
        result.front() = static_cast<char>(
            std::toupper(static_cast<unsigned char>(result.front()))
        );
    }
    return result;
}

void begin_parameter_row(std::string_view name) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    const std::string display_name = parameter_display_name(name);
    ImGui::TextUnformatted(display_name.c_str());
    if (ImGui::IsItemHovered() && display_name != name) {
        ImGui::SetTooltip("%.*s", static_cast<int>(name.size()), name.data());
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
}

bool begin_parameter_table(const char* id) {
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable(id, 2, flags)) return false;
    ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthStretch, 0.48f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.52f);
    return true;
}

bool draw_uniform_parameter(
    const ShaderUniformParameter& parameter,
    std::vector<std::byte>& bytes
) {
    const std::size_t count = ShaderAsset::parameter_component_count(parameter.type);
    if (count == 0 || parameter.offset + count * sizeof(float) > bytes.size()) return false;

    float values[4]{};
    std::memcpy(values, bytes.data() + parameter.offset, count * sizeof(float));
    const bool has_range = parameter.minimum && parameter.maximum;
    const float minimum = has_range ? *parameter.minimum : 0.0f;
    const float maximum = has_range ? *parameter.maximum : 0.0f;
    bool changed = false;
    switch (parameter.type) {
        case ShaderUniformParameterType::Float:
            changed = has_range
                ? ImGui::SliderFloat("##value", values, minimum, maximum)
                : ImGui::DragFloat("##value", values, 0.01f);
            break;
        case ShaderUniformParameterType::Vec2:
            changed = ImGui::DragFloat2(
                "##value", values, 0.01f, minimum, maximum
            );
            break;
        case ShaderUniformParameterType::Vec4:
            changed = ImGui::DragFloat4(
                "##value", values, 0.01f, minimum, maximum
            );
            break;
        case ShaderUniformParameterType::Color:
            changed = ImGui::ColorEdit4("##value", values);
            break;
    }
    if (changed) {
        std::memcpy(bytes.data() + parameter.offset, values, count * sizeof(float));
    }
    return changed;
}

std::optional<std::uint64_t> json_unsigned_id(const nlohmann::json& value) {
    if (value.is_number_unsigned()) return value.get<std::uint64_t>();
    if (value.is_number_integer()) {
        const auto result = value.get<std::int64_t>();
        if (result >= 0) return static_cast<std::uint64_t>(result);
    }
    if (!value.is_string()) return std::nullopt;
    try {
        std::size_t consumed = 0;
        const std::string text = value.get<std::string>();
        const auto result = std::stoull(text, &consumed);
        if (consumed == text.size()) return result;
    } catch (...) {
    }
    return std::nullopt;
}

nlohmann::json script_field_json_value(const ScriptFieldValue& value) {
    if (const auto* typed = std::get_if<bool>(&value)) return *typed;
    if (const auto* typed = std::get_if<double>(&value)) return *typed;
    if (const auto* typed = std::get_if<std::string>(&value)) return *typed;
    if (const auto* typed = std::get_if<ScriptVector2>(&value)) {
        return nlohmann::json::array({typed->x, typed->y});
    }
    if (const auto* typed = std::get_if<EntityID>(&value)) return *typed;
    if (const auto* typed = std::get_if<AssetHandle>(&value)) return typed->to_string();
    return nullptr;
}

} // namespace

VfxPreviewPanel::VfxPreviewPanel(
    SDL_Renderer* renderer,
    AssetDatabase* asset_database,
    SceneManager* scene_manager,
    std::shared_ptr<IMaterialBackend> material_backend
) : m_renderer(renderer),
    m_asset_database(asset_database),
    m_scene_manager(scene_manager),
    m_material_backend(std::move(material_backend)) {
    m_preview_camera.set_reference_size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
    m_preview_camera.set_viewport_size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
    m_particle_world.set_renderer(renderer);
}

VfxPreviewPanel::~VfxPreviewPanel() {
    stop_preview();
    destroy_render_resources();
}

bool VfxPreviewPanel::preview_blocked_by_play() const {
    return m_scene_manager && m_scene_manager->is_play_session_active();
}

bool VfxPreviewPanel::has_live_content() const {
    if (!m_scene) return false;
    const auto& entities = m_scene->get_entity_manager();
    return !entities.get_living_entities().empty() ||
        m_particle_world.system().get_active_count() > 0 ||
        m_trail_system.total_point_count() > 0;
}

void VfxPreviewPanel::open_asset(AssetHandle handle) {
    if (!handle.is_valid() || !m_asset_database) return;
    const AssetRecord* record = m_asset_database->find(handle);
    if (!record || record->type != AssetType::Prefab ||
        record->status != AssetStatus::Ready) return;

    if (m_asset_handle != handle) {
        m_script_parameter_overrides.clear();
        m_material_parameter_drafts.clear();
        m_modified_materials.clear();
        m_save_feedback.clear();
    }
    m_asset_handle = handle;
    m_asset_name = std::filesystem::path(record->source_path).stem().string();
    is_open = true;
    m_focus_requested = true;
    m_playing = true;
    if (preview_blocked_by_play()) {
        stop_preview();
        m_suspended_for_play = true;
        return;
    }
    m_suspended_for_play = false;
    static_cast<void>(rebuild_preview());
}

void VfxPreviewPanel::on_visibility_changed(bool visible) {
    if (!visible) {
        stop_preview();
    } else if (m_asset_handle.is_valid() && !preview_blocked_by_play() && !m_scene) {
        static_cast<void>(rebuild_preview());
    }
}

void VfxPreviewPanel::stop_preview() {
    m_script_runtime.shutdown();
    m_particle_world.clear();
    m_trail_system.clear();
    m_scene.reset();
    m_root_entity = 0;
    m_local_to_runtime.clear();
    m_starting = false;
    m_pending_seek.reset();
    m_elapsed = 0.0f;
    m_timeline_cursor = 0.0f;
    m_last_step_delta = 0.0f;
    m_frame_index = 0;
}

std::string VfxPreviewPanel::script_parameter_key(
    EntityID local_entity,
    ScriptSlotID slot_id,
    const std::string& field_name
) {
    return std::to_string(local_entity) + ":" + std::to_string(slot_id) + ":" + field_name;
}

void VfxPreviewPanel::apply_script_parameters(
    const std::unordered_map<EntityID, EntityID>& local_to_runtime
) {
    if (!m_scene) return;
    auto& entities = m_scene->get_entity_manager();
    for (const auto& [local_entity, runtime_entity] : local_to_runtime) {
        if (!entities.is_alive(runtime_entity) ||
            !entities.has_component<ScriptComponent>(runtime_entity)) continue;
        auto& scripts = entities.get_component<ScriptComponent>(runtime_entity);
        for (ScriptBehaviourSlot& slot : scripts.instances) {
            for (ScriptPublicField& field : slot.public_fields) {
                const auto found = m_script_parameter_overrides.find(
                    script_parameter_key(local_entity, slot.id, field.name)
                );
                if (found != m_script_parameter_overrides.end()) field.value = found->second;
            }
        }
    }
}

void VfxPreviewPanel::collect_material_parameters() {
    if (!m_scene || !m_asset_database) return;
    auto& entities = m_scene->get_entity_manager();
    std::unordered_set<AssetHandle> handles;
    entities.for_each<SpriteComponent>([&](EntityID, const SpriteComponent& sprite) {
        if (sprite.material_handle.is_valid()) handles.insert(sprite.material_handle);
    });
    entities.for_each<TrailRenderer2DComponent>([&](
        EntityID, const TrailRenderer2DComponent& trail
    ) {
        if (trail.material_handle.is_valid()) handles.insert(trail.material_handle);
    });

    for (const AssetHandle handle : handles) {
        if (m_material_parameter_drafts.contains(handle)) continue;
        std::string error;
        const auto material = MaterialAsset::read(handle, *m_asset_database, &error);
        if (!material) continue;
        const auto shader = ShaderAsset::read(
            material->shader_handle, *m_asset_database, &error
        );
        if (!shader) continue;
        m_material_parameter_drafts.emplace(
            handle, PreviewMaterialDraft{*material, *shader}
        );
    }
}

void VfxPreviewPanel::apply_material_parameters() {
    if (!m_scene || !m_asset_database) return;
    auto& entities = m_scene->get_entity_manager();
    for (const auto& [handle, draft] : m_material_parameter_drafts) {
        std::string error;
        const auto material = MaterialAsset::instantiate(
            draft.material, *m_asset_database, &error
        );
        if (material) {
            static_cast<void>(MaterialAsset::preview_assigned(
                entities, handle, material
            ));
        }
    }
}

void VfxPreviewPanel::reset_parameters() {
    m_script_parameter_overrides.clear();
    m_material_parameter_drafts.clear();
    m_modified_materials.clear();
    m_save_feedback.clear();
    static_cast<void>(rebuild_preview());
}

void VfxPreviewPanel::save_parameters() {
    if (!m_asset_database || !m_asset_handle.is_valid()) return;

    bool saved_any = false;
    std::vector<std::string> errors;

    if (!m_script_parameter_overrides.empty()) {
        const AssetRecord* record = m_asset_database->find(m_asset_handle);
        if (!record || record->type != AssetType::Prefab) {
            errors.emplace_back("prefab asset is unavailable");
        } else {
            const std::filesystem::path prefab_path = m_asset_database->resolve(m_asset_handle);
            const std::string source = FileSystem::read_text(prefab_path);
            auto document = nlohmann::json::parse(source, nullptr, false);
            if (document.is_discarded() || !document.contains("entities") ||
                !document["entities"].is_array()) {
                errors.emplace_back("prefab JSON is invalid");
            } else {
                std::unordered_set<std::string> applied;
                for (auto& entity : document["entities"]) {
                    if (!entity.is_object() || !entity.contains("id")) continue;
                    const auto local_entity = json_unsigned_id(entity["id"]);
                    if (!local_entity || !entity.contains("script") ||
                        !entity["script"].is_object()) continue;
                    auto& instances = entity["script"]["instances"];
                    if (!instances.is_array()) continue;
                    for (auto& instance : instances) {
                        if (!instance.is_object() || !instance.contains("id")) continue;
                        const auto slot_id = json_unsigned_id(instance["id"]);
                        if (!slot_id || !instance.contains("public_fields")) continue;
                        auto& fields = instance["public_fields"];
                        if (!fields.is_array()) continue;
                        for (auto& field : fields) {
                            if (!field.is_object() || !field.contains("name") ||
                                !field["name"].is_string()) continue;
                            const std::string key = script_parameter_key(
                                static_cast<EntityID>(*local_entity),
                                static_cast<ScriptSlotID>(*slot_id),
                                field["name"].get<std::string>()
                            );
                            const auto override = m_script_parameter_overrides.find(key);
                            if (override == m_script_parameter_overrides.end()) continue;
                            field["value"] = script_field_json_value(override->second);
                            applied.insert(key);
                        }
                    }
                }
                if (applied.size() != m_script_parameter_overrides.size()) {
                    errors.emplace_back("some script fields no longer exist in the prefab");
                } else if (!FileSystem::write_text_atomically(
                               prefab_path, document.dump(4) + "\n")) {
                    errors.emplace_back("failed writing prefab parameters");
                } else {
                    const bool imported = m_asset_database->import_asset(prefab_path).has_value();
                    if (!imported) {
                        errors.emplace_back("prefab was written but could not be reimported");
                    } else {
                        m_script_parameter_overrides.clear();
                        saved_any = true;
                    }
                }
            }
        }
    }

    std::vector<AssetHandle> material_handles(
        m_modified_materials.begin(), m_modified_materials.end()
    );
    for (const AssetHandle handle : material_handles) {
        const auto draft = m_material_parameter_drafts.find(handle);
        if (draft == m_material_parameter_drafts.end()) continue;
        std::string error;
        if (MaterialAsset::save(handle, draft->second.material, *m_asset_database, &error)) {
            m_modified_materials.erase(handle);
            saved_any = true;
        } else {
            errors.push_back(error.empty() ? "failed writing material parameters" : error);
        }
    }

    m_save_feedback_is_error = !errors.empty();
    if (errors.empty()) {
        m_save_feedback = saved_any ? "Saved to source assets" : "Nothing to save";
    } else {
        m_save_feedback = "Could not save: " + errors.front();
    }
}

EntityID VfxPreviewPanel::instantiate_prefab(
    AssetHandle handle,
    const glm::vec2& position
) {
    if (!m_scene || !m_asset_database || !handle.is_valid()) return 0;
    const AssetRecord* record = m_asset_database->find(handle);
    if (!record || record->type != AssetType::Prefab ||
        record->status != AssetStatus::Ready) return 0;
    const std::string source = PrefabSerializer::read_json_from_file(
        m_asset_database->resolve(handle)
    );
    if (source.empty()) return 0;

    auto& entities = m_scene->get_entity_manager();
    ComponentSerializationContext context{
        .renderer = m_renderer,
        .asset_manager = &AssetManager::get(),
        .asset_database = m_asset_database
    };
    const SubtreeInstantiationResult result = EntitySubtreeArchive::instantiate_from_json(
        entities, source, std::nullopt, 0, context, &default_component_registry()
    );
    if (!result.success || result.root_entity == 0) return 0;
    if (entities.has_component<TransformComponent>(result.root_entity)) {
        auto& transform = entities.get_component<TransformComponent>(result.root_entity);
        transform.position = {position.x, position.y, 0.0f};
    }
    TransformSystem::update(entities);
    collect_material_parameters();
    apply_material_parameters();
    for (const EntityID entity : result.created_entities) {
        if (m_script_runtime.is_running()) m_script_runtime.queue_entity_start(entity);
    }
    return result.root_entity;
}

bool VfxPreviewPanel::rebuild_preview(std::optional<float> seek_time) {
    stop_preview();
    m_pending_seek = seek_time;
    m_error.clear();
    m_script_count = 0;
    m_script_failures = 0;
    if (!m_asset_database || !m_asset_handle.is_valid()) return false;
    const AssetRecord* record = m_asset_database->find(m_asset_handle);
    if (!record || record->type != AssetType::Prefab ||
        record->status != AssetStatus::Ready) {
        m_error = "The selected prefab is unavailable";
        return false;
    }

    const std::string source = PrefabSerializer::read_json_from_file(
        m_asset_database->resolve(m_asset_handle)
    );
    if (source.empty()) {
        m_error = "Could not read the prefab";
        return false;
    }

    m_scene = std::make_unique<Scene>("VFX Preview: " + m_asset_name);
    m_scene->settings().physics_enabled = false;
    auto& entities = m_scene->get_entity_manager();
    ComponentSerializationContext context{
        .renderer = m_renderer,
        .asset_manager = &AssetManager::get(),
        .asset_database = m_asset_database
    };
    const SubtreeInstantiationResult instantiated = EntitySubtreeArchive::instantiate_from_json(
        entities, source, std::nullopt, 0, context, &default_component_registry()
    );
    if (!instantiated.success || instantiated.root_entity == 0) {
        m_error = instantiated.error_message.empty()
            ? "Could not instantiate the prefab"
            : instantiated.error_message;
        m_scene.reset();
        return false;
    }
    m_root_entity = instantiated.root_entity;
    m_local_to_runtime = instantiated.local_to_runtime;
    apply_script_parameters(m_local_to_runtime);
    collect_material_parameters();
    apply_material_parameters();
    TransformSystem::update(entities);

    m_duration = 2.0f;
    entities.for_each<ScriptComponent>([&](EntityID, ScriptComponent& scripts) {
        for (const ScriptBehaviourSlot& slot : scripts.instances) {
            if (!slot.enabled) continue;
            ++m_script_count;
            for (const ScriptPublicField& field : slot.public_fields) {
                if (field.name == "duration" && field.type == ScriptFieldType::Number &&
                    std::holds_alternative<double>(field.value)) {
                    m_duration = std::clamp(
                        static_cast<float>(std::get<double>(field.value)), 0.1f, 30.0f
                    );
                }
            }
        }
    });

    m_particle_world.set_renderer(m_renderer);
    m_particle_world.system().set_seed(m_particle_seed);
    entities.for_each<ParticleEmitter2DComponent>([&](
        EntityID entity, ParticleEmitter2DComponent& emitter
    ) {
        if (emitter.play_on_awake) {
            static_cast<void>(m_particle_world.play_entity(
                entity, *m_scene, *m_asset_database
            ));
        }
    });

    fit_preview();
    if (m_script_count == 0) {
        finish_startup({});
        return true;
    }

    ScriptRuntimeServices services;
    services.async_native_build = true;
    services.current_scene_name = [this]() { return m_scene ? m_scene->get_name() : std::string{}; };
    services.is_scene_loading = []() { return false; };
    services.scene_load_error = []() { return std::string{}; };
    services.preload_asset = [this](AssetHandle handle, std::string& error) {
        const AssetRecord* asset = m_asset_database ? m_asset_database->find(handle) : nullptr;
        if (asset && asset->status == AssetStatus::Ready) return true;
        error = "Preview asset is unavailable";
        return false;
    };
    services.instantiate_prefab = [this](AssetHandle handle, const glm::vec2& position) {
        return instantiate_prefab(handle, position);
    };
    services.create_prefab = [this](AssetHandle handle) {
        const EntityID entity = instantiate_prefab(handle, {});
        if (entity != 0) m_script_runtime.prepare_entity(entity);
        return entity;
    };
    services.spawn_prefab = [this](EntityID entity, const glm::vec2& position) {
        if (!m_scene) return false;
        auto& preview_entities = m_scene->get_entity_manager();
        if (!preview_entities.is_alive(entity) ||
            !preview_entities.has_component<TransformComponent>(entity)) return false;
        auto& transform = preview_entities.get_component<TransformComponent>(entity);
        transform.position = {position.x, position.y, 0.0f};
        TransformSystem::update(preview_entities);
        return m_script_runtime.activate_entity(entity);
    };
    services.emit_particles = [this](
        AssetHandle handle, const glm::vec2& position, int count
    ) {
        return m_asset_database && m_particle_world.emit_asset(
            handle, position, *m_asset_database, count
        );
    };
    services.emit_particles_with_options = [this](
        AssetHandle handle,
        const glm::vec2& position,
        const EngineScript::ParticleEmitOptions& options
    ) {
        if (!m_asset_database) return false;
        ParticleEmissionOverrides overrides;
        overrides.count = options.count;
        if (options.overrideVelocity) overrides.velocity = options.velocity;
        if (options.overrideVelocityVariation) {
            overrides.velocity_variation = options.velocityVariation;
        }
        if (options.overrideColor) {
            const auto channel = [](float value) {
                return static_cast<std::uint8_t>(
                    std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f
                );
            };
            overrides.color = SDL_Color{
                channel(options.color.x), channel(options.color.y),
                channel(options.color.z), channel(options.color.w)
            };
        }
        overrides.scale = options.scale;
        overrides.rotation = options.rotation;
        return m_particle_world.emit_asset(
            handle, position, *m_asset_database, overrides
        );
    };
    services.play_particle_emitter = [this](EntityID entity) {
        return m_scene && m_asset_database && m_particle_world.play_entity(
            entity, *m_scene, *m_asset_database
        );
    };
    services.stop_particle_emitter = [this](EntityID entity, bool clear) {
        return m_scene && m_particle_world.stop_entity(entity, *m_scene, clear);
    };
    services.burst_particle_emitter = [this](EntityID entity, int count) {
        return m_scene && m_asset_database && m_particle_world.burst_entity(
            entity, *m_scene, *m_asset_database, count
        );
    };

    const ScriptRuntimeStartResult started = m_script_runtime.start(
        entities, *m_asset_database, &m_scene->get_event_dispatcher(), std::move(services)
    );
    m_starting = started.pending;
    if (!started.pending) finish_startup(started);
    return true;
}

void VfxPreviewPanel::finish_startup(const ScriptRuntimeStartResult& result) {
    m_starting = false;
    m_script_failures = result.failed;
    if (result.failed > 0) {
        m_error = std::to_string(result.failed) + " preview script" +
            (result.failed == 1 ? " failed" : "s failed");
    }
    const std::optional<float> seek = m_pending_seek;
    m_pending_seek.reset();
    if (seek && *seek > 0.0f) simulate(*seek);
}

void VfxPreviewPanel::restart_preview() {
    static_cast<void>(rebuild_preview());
}

void VfxPreviewPanel::seek_to(float target_time) {
    const bool was_playing = m_playing;
    m_playing = false;
    static_cast<void>(rebuild_preview(std::clamp(target_time, 0.0f, m_duration)));
    m_playing = was_playing;
}

void VfxPreviewPanel::simulate_step(float delta_time) {
    if (!m_scene || m_starting || delta_time <= 0.0f) return;
    m_particle_world.begin_frame();
    if (m_script_runtime.is_running()) m_script_runtime.update(delta_time);
    auto& entities = m_scene->get_entity_manager();
    TransformSystem::update(entities);
    SpriteAnimationSystem::update(entities, delta_time);
    m_particle_world.update(*m_scene, delta_time, *m_asset_database);
    m_trail_system.update(entities, delta_time);
    m_scene->get_event_dispatcher().dispatch_queued();
    if (m_script_runtime.is_running()) m_script_runtime.dispatch_events();
    m_elapsed += delta_time;
    m_timeline_cursor = m_elapsed;
    m_last_step_delta = delta_time;
    ++m_frame_index;
}

void VfxPreviewPanel::simulate(float delta_time) {
    float remaining = std::max(0.0f, delta_time);
    while (remaining > 0.000001f && m_scene) {
        const float step = std::min(FIXED_STEP, remaining);
        simulate_step(step);
        remaining -= step;
    }
}

void VfxPreviewPanel::on_update(float delta_time) {
    if (preview_blocked_by_play()) {
        if (m_scene || m_script_runtime.is_running() || m_starting) stop_preview();
        m_suspended_for_play = m_asset_handle.is_valid();
        return;
    }
    if (m_suspended_for_play && m_asset_handle.is_valid()) {
        m_suspended_for_play = false;
        static_cast<void>(rebuild_preview());
    }
    if (m_starting) {
        const ScriptRuntimeStartResult result = m_script_runtime.poll_start();
        if (!result.pending) finish_startup(result);
        return;
    }
    if (!m_scene || !m_playing) return;
    simulate(std::clamp(delta_time, 0.0f, 0.1f) * m_speed);
    if (m_elapsed >= m_duration || !has_live_content()) {
        if (m_loop) restart_preview();
        else {
            m_playing = false;
            m_timeline_cursor = std::min(m_elapsed, m_duration);
        }
    }
}

void VfxPreviewPanel::fit_preview() {
    if (!m_scene) return;
    auto& entities = m_scene->get_entity_manager();
    bool found = false;
    glm::vec2 minimum{std::numeric_limits<float>::max()};
    glm::vec2 maximum{std::numeric_limits<float>::lowest()};
    entities.for_each<TransformComponent, SpriteComponent>([&](
        EntityID, const TransformComponent& transform, const SpriteComponent& sprite
    ) {
        if (!transform.world_visible || !sprite.visible) return;
        const glm::vec2 size = glm::abs(transform.world_scale * sprite.size);
        const glm::vec2 position{transform.world_position.x, transform.world_position.y};
        minimum = glm::min(minimum, position - size * 0.5f);
        maximum = glm::max(maximum, position + size * 0.5f);
        found = true;
    });
    if (!found) {
        m_camera_position = {};
        m_zoom = 1.0f;
        return;
    }
    m_camera_position = (minimum + maximum) * 0.5f;
    const glm::vec2 extent = glm::max(maximum - minimum, glm::vec2{32.0f});
    m_zoom = std::clamp(std::min(
        static_cast<float>(PREVIEW_WIDTH) / (extent.x * 3.0f),
        static_cast<float>(PREVIEW_HEIGHT) / (extent.y * 3.0f)
    ), 0.1f, 8.0f);
}

void VfxPreviewPanel::destroy_render_resources() {
    m_preview_renderer.reset();
    if (m_preview_target) SDL_DestroyTexture(m_preview_target);
    m_preview_target = nullptr;
}

void VfxPreviewPanel::on_render_resources_invalidated() {
    destroy_render_resources();
    m_particle_world.invalidate_render_resources();
    m_trail_system.clear();
}

void VfxPreviewPanel::render_toolbar() {
    if (EditorUi::toolbar_button(
            "vfx_preview_play",
            m_playing ? EditorIcon::Pause : EditorIcon::Play,
            m_playing ? "Pause preview" : "Play preview",
            m_playing,
            !m_starting)) {
        m_playing = !m_playing;
    }
    ImGui::SameLine();
    if (EditorUi::action_button(
            "vfx_preview_restart", EditorIcon::History, "Restart",
            EditorUi::ButtonTone::Neutral, !m_starting)) {
        restart_preview();
    }
    ImGui::SameLine();
    if (EditorUi::action_button(
            "vfx_preview_step", EditorIcon::StepForward, "Step one frame",
            EditorUi::ButtonTone::Neutral, !m_starting)) {
        m_playing = false;
        if (m_elapsed + FIXED_STEP > m_duration) seek_to(FIXED_STEP);
        else simulate_step(FIXED_STEP);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &m_loop);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(82.0f * EditorTheme::metrics().scale);
    ImGui::SliderFloat("##vfx_preview_speed", &m_speed, 0.1f, 2.0f, "%.1fx");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playback speed");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(104.0f * EditorTheme::metrics().scale);
    if (ImGui::InputScalar(
            "##vfx_particle_seed", ImGuiDataType_U32, &m_particle_seed)) {
        restart_preview();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Deterministic particle seed");
    ImGui::SameLine();
    if (EditorUi::action_button(
            "vfx_preview_random_seed", EditorIcon::Refresh, "New particle seed",
            EditorUi::ButtonTone::Neutral)) {
        m_particle_seed = m_particle_seed * 1664525u + 1013904223u;
        restart_preview();
    }

    ImGui::SameLine();
    float background[3] = {
        color_channel(m_background.r), color_channel(m_background.g),
        color_channel(m_background.b)
    };
    if (ImGui::ColorEdit3(
            "##vfx_preview_background", background,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        m_background = {
            color_byte(background[0]), color_byte(background[1]),
            color_byte(background[2]), 255
        };
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Preview background color");
    ImGui::SameLine();
    if (EditorUi::action_button(
            "vfx_preview_fit", EditorIcon::Crosshair, "Fit prefab",
            EditorUi::ButtonTone::Neutral)) {
        fit_preview();
    }

    const std::string status = m_starting
        ? "BUILDING"
        : std::to_string(m_script_count) + " SCRIPTS";
    const float badge_width = ImGui::CalcTextSize(status.c_str()).x +
        10.0f * EditorTheme::metrics().scale;
    ImGui::SameLine(std::max(
        ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - badge_width
    ));
    EditorUi::badge(
        status.c_str(),
        m_script_failures > 0 ? EditorUi::BadgeTone::Error
            : (m_starting ? EditorUi::BadgeTone::Warning : EditorUi::BadgeTone::Neutral)
    );
    ImGui::Separator();
}

void VfxPreviewPanel::render_timeline() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", m_asset_name.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-155.0f * EditorTheme::metrics().scale);
    if (ImGui::SliderFloat(
            "##vfx_preview_timeline", &m_timeline_cursor,
            0.0f, m_duration, "%.3f s")) {
        m_playing = false;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) seek_to(m_timeline_cursor);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag and release to rebuild at this time");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(86.0f * EditorTheme::metrics().scale);
    ImGui::DragFloat("##vfx_preview_duration", &m_duration, 0.05f, 0.1f, 30.0f, "%.2f s");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Preview duration");
    ImGui::SameLine();
    ImGui::TextDisabled("#%llu", static_cast<unsigned long long>(m_frame_index));
    ImGui::Separator();
}

void VfxPreviewPanel::render_parameters() {
    ImGui::BeginChild(
        "##vfx_preview_parameters", ImVec2(0.0f, 0.0f), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar
    );

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("PARAMETERS");
    const bool has_overrides = !m_script_parameter_overrides.empty() ||
        !m_modified_materials.empty();
    if (has_overrides) {
        ImGui::SameLine();
        EditorUi::badge("UNSAVED", EditorUi::BadgeTone::Warning);
    }
    ImGui::SameLine(std::max(
        ImGui::GetCursorPosX(),
        ImGui::GetWindowContentRegionMax().x - 105.0f * EditorTheme::metrics().scale
    ));
    ImGui::BeginDisabled(!has_overrides || m_starting);
    if (ImGui::SmallButton("Save")) save_parameters();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Save changed values to the prefab and material assets");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!has_overrides || m_starting);
    if (ImGui::SmallButton("Reset")) reset_parameters();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Restore values from the prefab and material assets");
    }
    ImGui::Separator();
    if (!m_save_feedback.empty()) {
        ImGui::TextColored(
            m_save_feedback_is_error
                ? EditorTheme::palette().error
                : EditorTheme::palette().success,
            "%s", m_save_feedback.c_str()
        );
    }

    bool restart_requested = false;
    bool found_script_parameter = false;
    if (ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::vector<std::pair<EntityID, EntityID>> mappings(
            m_local_to_runtime.begin(), m_local_to_runtime.end()
        );
        std::sort(mappings.begin(), mappings.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        if (m_scene) {
            auto& entities = m_scene->get_entity_manager();
            for (const auto& [local_entity, runtime_entity] : mappings) {
                if (!entities.is_alive(runtime_entity) ||
                    !entities.has_component<ScriptComponent>(runtime_entity)) continue;
                auto& scripts = entities.get_component<ScriptComponent>(runtime_entity);
                const std::string entity_name = entities.has_component<TagComponent>(runtime_entity)
                    ? entities.get_component<TagComponent>(runtime_entity).tag
                    : "Entity #" + std::to_string(runtime_entity);
                for (ScriptBehaviourSlot& slot : scripts.instances) {
                    const bool has_exposed_fields = std::any_of(
                        slot.public_fields.begin(), slot.public_fields.end(),
                        [](const ScriptPublicField& field) { return field.is_exposed; }
                    );
                    if (!has_exposed_fields) continue;
                    found_script_parameter = true;
                    std::string script_name = "Script";
                    if (m_asset_database && slot.script_handle.is_valid()) {
                        if (const AssetRecord* record = m_asset_database->find(slot.script_handle)) {
                            script_name = std::filesystem::path(record->source_path).stem().string();
                        }
                    }
                    const std::string group = entity_name + " / " + script_name;
                    ImGui::PushID(group.c_str());
                    const bool open = ImGui::TreeNodeEx(
                        "##script_parameters", ImGuiTreeNodeFlags_DefaultOpen,
                        "%s", group.c_str()
                    );
                    if (open) {
                        if (begin_parameter_table("##script_parameter_table")) {
                            for (ScriptPublicField& field : slot.public_fields) {
                                if (!field.is_exposed) continue;
                                ImGui::PushID(field.name.c_str());
                                begin_parameter_row(field.name);
                                bool changed = false;
                                bool discrete = false;
                                switch (field.type) {
                                    case ScriptFieldType::Boolean: {
                                        bool value = std::get<bool>(field.value);
                                        changed = ImGui::Checkbox("##value", &value);
                                        if (changed) field.value = value;
                                        discrete = true;
                                        break;
                                    }
                                    case ScriptFieldType::Number: {
                                        float value = static_cast<float>(std::get<double>(field.value));
                                        const bool has_range = field.minimum && field.maximum;
                                        changed = has_range
                                            ? ImGui::SliderFloat(
                                                "##value", &value,
                                                static_cast<float>(*field.minimum),
                                                static_cast<float>(*field.maximum), "%.3f"
                                            )
                                            : ImGui::DragFloat(
                                                "##value", &value, 0.01f, 0.0f, 0.0f, "%.3f"
                                            );
                                        if (changed) field.value = static_cast<double>(value);
                                        break;
                                    }
                                    case ScriptFieldType::String: {
                                        std::array<char, 512> buffer{};
                                        std::snprintf(
                                            buffer.data(), buffer.size(), "%s",
                                            std::get<std::string>(field.value).c_str()
                                        );
                                        changed = ImGui::InputText(
                                            "##value", buffer.data(), buffer.size(),
                                            ImGuiInputTextFlags_EnterReturnsTrue
                                        );
                                        if (changed) field.value = std::string(buffer.data());
                                        discrete = true;
                                        break;
                                    }
                                    case ScriptFieldType::Enum: {
                                        const std::string current = std::get<std::string>(field.value);
                                        if (ImGui::BeginCombo("##value", current.c_str())) {
                                            for (const std::string& option : field.enum_values) {
                                                const bool selected = option == current;
                                                if (ImGui::Selectable(option.c_str(), selected)) {
                                                    field.value = option;
                                                    changed = true;
                                                }
                                                if (selected) ImGui::SetItemDefaultFocus();
                                            }
                                            ImGui::EndCombo();
                                        }
                                        discrete = true;
                                        break;
                                    }
                                    case ScriptFieldType::Vector2: {
                                        const ScriptVector2 current = std::get<ScriptVector2>(field.value);
                                        float values[2]{current.x, current.y};
                                        const float minimum = field.minimum
                                            ? static_cast<float>(*field.minimum) : 0.0f;
                                        const float maximum = field.maximum
                                            ? static_cast<float>(*field.maximum) : 0.0f;
                                        changed = ImGui::DragFloat2(
                                            "##value", values, 0.01f, minimum, maximum
                                        );
                                        if (changed) field.value = ScriptVector2{values[0], values[1]};
                                        break;
                                    }
                                    case ScriptFieldType::Entity:
                                        ImGui::TextDisabled(
                                            "Entity #%u", std::get<EntityID>(field.value)
                                        );
                                        if (ImGui::IsItemHovered()) {
                                            ImGui::SetTooltip("References are read-only in isolated preview");
                                        }
                                        break;
                                    case ScriptFieldType::Asset:
                                        ImGui::TextDisabled(
                                            "%s", std::get<AssetHandle>(field.value).to_string().c_str()
                                        );
                                        if (ImGui::IsItemHovered()) {
                                            ImGui::SetTooltip("References are read-only in isolated preview");
                                        }
                                        break;
                                }
                                if (changed) {
                                    m_save_feedback.clear();
                                    m_script_parameter_overrides[
                                        script_parameter_key(local_entity, slot.id, field.name)
                                    ] = field.value;
                                    if (field.name == "duration" &&
                                        field.type == ScriptFieldType::Number) {
                                        m_duration = std::clamp(
                                            static_cast<float>(std::get<double>(field.value)),
                                            0.1f, 30.0f
                                        );
                                    }
                                    restart_requested |= discrete ||
                                        ImGui::IsItemDeactivatedAfterEdit();
                                } else if (!discrete && ImGui::IsItemDeactivatedAfterEdit()) {
                                    restart_requested = true;
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }
        }
        if (!found_script_parameter) {
            ImGui::TextDisabled("No exposed script fields.");
        }
    }

    bool material_changed = false;
    bool found_material_parameter = false;
    if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::vector<AssetHandle> handles;
        handles.reserve(m_material_parameter_drafts.size());
        for (const auto& [handle, draft] : m_material_parameter_drafts) {
            static_cast<void>(draft);
            handles.push_back(handle);
        }
        std::sort(handles.begin(), handles.end(), [this](AssetHandle left, AssetHandle right) {
            return m_material_parameter_drafts.at(left).material.name <
                m_material_parameter_drafts.at(right).material.name;
        });
        for (const AssetHandle handle : handles) {
            auto& draft = m_material_parameter_drafts.at(handle);
            const std::string handle_id = handle.to_string();
            ImGui::PushID(handle_id.c_str());
            for (const auto& layout : draft.shader.uniform_buffers) {
                if (layout.source == ShaderUniformBufferSource::Material &&
                    !layout.parameters.empty()) {
                    found_material_parameter = true;
                }
            }
            const bool open = ImGui::TreeNodeEx(
                "##material_parameters", ImGuiTreeNodeFlags_DefaultOpen,
                "%s", draft.material.name.c_str()
            );
            if (open) {
                std::size_t material_index = 0;
                for (std::size_t shader_slot = 0;
                     shader_slot < draft.shader.uniform_buffers.size();
                     ++shader_slot) {
                    const auto& layout = draft.shader.uniform_buffers[shader_slot];
                    if (layout.source == ShaderUniformBufferSource::Engine) continue;
                    if (material_index >= draft.material.uniforms.size()) break;
                    if (!layout.parameters.empty()) {
                        const std::string layout_name = parameter_display_name(layout.name);
                        ImGui::TextDisabled("%s", layout_name.c_str());
                        ImGui::PushID(static_cast<int>(shader_slot));
                        if (begin_parameter_table("##material_parameter_table")) {
                            for (const ShaderUniformParameter& parameter : layout.parameters) {
                                ImGui::PushID(parameter.name.c_str());
                                begin_parameter_row(parameter.name);
                                if (draw_uniform_parameter(
                                        parameter, draft.material.uniforms[material_index])) {
                                    m_save_feedback.clear();
                                    material_changed = true;
                                    m_modified_materials.insert(handle);
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                        ImGui::PopID();
                    }
                    ++material_index;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (!found_material_parameter) {
            ImGui::TextDisabled("No friendly material properties.");
        }
    }

    if (material_changed) apply_material_parameters();
    if (restart_requested && !m_starting) {
        const bool was_playing = m_playing;
        const float target = std::clamp(m_elapsed, 0.0f, m_duration);
        m_playing = false;
        static_cast<void>(rebuild_preview(target));
        m_playing = was_playing;
    }
    ImGui::EndChild();
}

void VfxPreviewPanel::render_to_target() {
    if (!m_renderer || !m_scene) return;
    if (!m_preview_target) {
        m_preview_target = SDL_CreateTexture(
            m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
            PREVIEW_WIDTH, PREVIEW_HEIGHT
        );
        if (m_preview_target) {
            static_cast<void>(SDL_SetTextureBlendMode(m_preview_target, SDL_BLENDMODE_BLEND));
            static_cast<void>(SDL_SetTextureScaleMode(m_preview_target, SDL_SCALEMODE_LINEAR));
        }
    }
    if (!m_preview_renderer) {
        m_preview_renderer = std::make_unique<Renderer2D>(m_renderer);
        m_preview_renderer->set_material_backend(m_material_backend);
    }
    if (!m_preview_target || !m_preview_renderer) return;

    SDL_Texture* previous_target = SDL_GetRenderTarget(m_renderer);
    SDL_Rect previous_viewport{};
    const bool had_viewport = SDL_GetRenderViewport(m_renderer, &previous_viewport);
    float previous_scale_x = 1.0f;
    float previous_scale_y = 1.0f;
    static_cast<void>(SDL_GetRenderScale(
        m_renderer, &previous_scale_x, &previous_scale_y
    ));
    std::uint8_t previous_r = 0, previous_g = 0, previous_b = 0, previous_a = 0;
    static_cast<void>(SDL_GetRenderDrawColor(
        m_renderer, &previous_r, &previous_g, &previous_b, &previous_a
    ));

    if (SDL_SetRenderTarget(m_renderer, m_preview_target)) {
        static_cast<void>(SDL_SetRenderScale(m_renderer, 1.0f, 1.0f));
        static_cast<void>(SDL_SetRenderViewport(m_renderer, nullptr));
        static_cast<void>(SDL_SetRenderDrawColor(
            m_renderer, m_background.r, m_background.g,
            m_background.b, m_background.a
        ));
        static_cast<void>(SDL_RenderClear(m_renderer));

        m_preview_camera.set_position(m_camera_position);
        m_preview_camera.set_zoom(m_zoom);
        m_preview_camera.set_viewport_size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
        m_preview_camera.set_reference_size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
        m_preview_renderer->set_custom_render_target(m_preview_target);
        m_preview_renderer->set_scene_clear_color(m_background);
        m_preview_renderer->set_shader_time_override(Renderer2D::ShaderTimeOverride{
            m_elapsed, m_elapsed, m_last_step_delta, static_cast<float>(m_frame_index)
        });
        m_preview_renderer->begin_scene(m_preview_camera);
        RenderSystem::render(m_scene->get_entity_manager(), *m_preview_renderer);
        m_trail_system.render(*m_preview_renderer);
        m_particle_world.render(*m_preview_renderer, m_scene->get_entity_manager());
        m_preview_renderer->end_scene();
        m_preview_renderer->set_shader_time_override(std::nullopt);
        m_preview_renderer->set_custom_render_target(previous_target);
    }

    static_cast<void>(SDL_SetRenderTarget(m_renderer, previous_target));
    static_cast<void>(SDL_SetRenderScale(
        m_renderer, previous_scale_x, previous_scale_y
    ));
    static_cast<void>(SDL_SetRenderViewport(
        m_renderer, had_viewport ? &previous_viewport : nullptr
    ));
    static_cast<void>(SDL_SetRenderDrawColor(
        m_renderer, previous_r, previous_g, previous_b, previous_a
    ));
}

void VfxPreviewPanel::render_canvas() {
    render_to_target();
    if (!m_preview_target) {
        EditorUi::empty_state(
            EditorIcon::Zap, "Preview unavailable",
            "Could not allocate the VFX render target"
        );
        return;
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(1.0f, available.x);
    available.y = std::max(1.0f, available.y);
    const float aspect = static_cast<float>(PREVIEW_WIDTH) /
        static_cast<float>(PREVIEW_HEIGHT);
    ImVec2 display_size(available.x, available.x / aspect);
    if (display_size.y > available.y) {
        display_size.y = available.y;
        display_size.x = display_size.y * aspect;
    }
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() + std::max(0.0f, (available.x - display_size.x) * 0.5f)
    );
    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + std::max(0.0f, (available.y - display_size.y) * 0.5f)
    );
    const ImVec2 image_min = ImGui::GetCursorScreenPos();
    ImGui::Image(m_preview_target, display_size);
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f) {
            m_zoom = std::clamp(m_zoom * std::pow(1.12f, io.MouseWheel), 0.1f, 8.0f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
            (ImGui::IsKeyDown(ImGuiKey_Space) &&
             ImGui::IsMouseDragging(ImGuiMouseButton_Left))) {
            const ImVec2 drag = io.MouseDelta;
            m_camera_position.x -= drag.x * static_cast<float>(PREVIEW_WIDTH) /
                std::max(display_size.x * m_zoom, 1.0f);
            m_camera_position.y += drag.y * static_cast<float>(PREVIEW_HEIGHT) /
                std::max(display_size.y * m_zoom, 1.0f);
        }
    }
    const ImVec2 image_max{
        image_min.x + display_size.x, image_min.y + display_size.y
    };
    ImGui::GetWindowDrawList()->AddRect(
        image_min, image_max,
        ImGui::ColorConvertFloat4ToU32(EditorTheme::palette().border),
        EditorTheme::metrics().small_rounding
    );
}

void VfxPreviewPanel::on_gui() {
    if (m_focus_requested) {
        ImGui::SetNextWindowFocus();
        m_focus_requested = false;
    }
    if (!ImGui::Begin(
            m_name.c_str(), &is_open,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }

    if (preview_blocked_by_play()) {
        EditorUi::empty_state(
            EditorIcon::Pause,
            "Preview suspended during Play",
            "Stop Play mode to resume the isolated VFX simulation"
        );
        ImGui::End();
        return;
    }
    if (!m_asset_handle.is_valid()) {
        EditorUi::empty_state(
            EditorIcon::Zap,
            "Select a VFX prefab",
            "Right-click a prefab in the Asset Browser and choose Open in VFX Preview"
        );
        ImGui::End();
        return;
    }
    if (!m_error.empty() && !m_scene) {
        EditorUi::empty_state(EditorIcon::Zap, "Preview failed", m_error.c_str());
        ImGui::End();
        return;
    }

    render_toolbar();
    if (ImGui::BeginTable(
            "##vfx_preview_layout", 2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed,
            340.0f * EditorTheme::metrics().scale);
        ImGui::TableNextColumn();
        render_timeline();
        if (!m_error.empty()) {
            ImGui::TextColored(EditorTheme::palette().error, "%s", m_error.c_str());
        }
        render_canvas();
        ImGui::TableNextColumn();
        render_parameters();
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace Engine
