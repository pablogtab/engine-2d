#include "RenderLayerEffectProfile.hpp"
#include "LocalEffectPass.hpp"
#include "material/IMaterialBackend.hpp"
#include "material/Material.hpp"
#include "material/MaterialAsset.hpp"
#include "../core/AssetDatabase.hpp"
#include "../core/FileSystem.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>

namespace Engine {
namespace {

void set_error(std::string* error, std::string value) { if (error) *error = std::move(value); }

class MaterialLocalEffectPass final : public ILocalEffectPass {
public:
    MaterialLocalEffectPass(std::shared_ptr<const Material> material, std::shared_ptr<IMaterialBackend> backend)
        : m_material(std::move(material)), m_backend(std::move(backend)) {}
    [[nodiscard]] const char* name() const override { return m_material->name().c_str(); }
    [[nodiscard]] bool is_enabled() const override { return true; }
    bool apply(const LocalEffectContext& context) override {
        if (!context.renderer || !context.destination ||
            !SDL_SetRenderTarget(context.renderer, context.destination)) return false;
        SDL_SetRenderDrawColor(context.renderer, 0, 0, 0, 0);
        SDL_RenderClear(context.renderer);
        const MaterialRenderContext material_context{context.width, context.height};
        if (m_backend && m_backend->draw_fullscreen(*m_material, context.source, material_context)) return true;
        return SDL_RenderTexture(context.renderer, context.source, nullptr, nullptr);
    }
private:
    std::shared_ptr<const Material> m_material;
    std::shared_ptr<IMaterialBackend> m_backend;
};

} // namespace

std::string RenderLayerEffectProfile::to_json() const {
    nlohmann::json serialized_passes = nlohmann::json::array();
    for (const auto& pass : passes) {
        serialized_passes.push_back({{"material_handle", pass.material_handle.to_string()}, {"enabled", pass.enabled}});
    }
    return nlohmann::json{{"name", name}, {"passes", std::move(serialized_passes)}}.dump(2) + "\n";
}

bool RenderLayerEffectProfile::from_json(const std::string& json_text, std::string* error) {
    try {
        const auto document = nlohmann::json::parse(json_text, nullptr, false);
        if (document.is_discarded() || !document.is_object() || !document.contains("passes") || !document["passes"].is_array()) {
            set_error(error, "Layer effect profile JSON is invalid"); return false;
        }
        if (document.contains("render_layer") || document.contains("enabled")) {
            set_error(error, "Layer and assignment state belong to the scene, not the reusable profile"); return false;
        }
        RenderLayerEffectProfile parsed;
        parsed.name = document.value("name", std::string{"Layer Effects"});
        if (parsed.name.empty()) { set_error(error, "Layer effect profile name cannot be empty"); return false; }
        for (const auto& value : document["passes"]) {
            if (!value.is_object() || !value.contains("material_handle") || !value["material_handle"].is_string()) {
                set_error(error, "Layer effect pass is invalid"); return false;
            }
            const auto handle = AssetHandle::from_string(value["material_handle"].get<std::string>());
            if (!handle || !handle->is_valid()) { set_error(error, "Layer effect material handle is invalid"); return false; }
            parsed.passes.push_back({*handle, value.value("enabled", true)});
        }
        *this = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        set_error(error, "Layer effect profile JSON has invalid field types");
        return false;
    }
}

bool RenderLayerEffectProfile::save_asset(const std::string& filepath, AssetDatabase& database, std::string* error) const {
    if (name.empty() || std::any_of(passes.begin(), passes.end(), [](const auto& pass) {
        return !pass.material_handle.is_valid();
    })) { set_error(error, "Layer effect profile contains invalid authoring data"); return false; }
    if (std::filesystem::path(filepath).extension() != ".layereffect") { set_error(error, "Layer effect assets use .layereffect"); return false; }
    if (!FileSystem::write_text_atomically(filepath, to_json())) { set_error(error, "Failed writing layer effect asset"); return false; }
    const auto handle = database.import_asset(filepath);
    return handle && database.find(*handle) && database.find(*handle)->type == AssetType::RenderLayerEffectProfile;
}

std::optional<RenderLayerEffectProfile> RenderLayerEffectProfile::load_asset(
    AssetHandle handle, const AssetDatabase& database, std::string* error
) {
    const AssetRecord* record = database.find(handle);
    if (!record || record->status != AssetStatus::Ready || record->type != AssetType::RenderLayerEffectProfile) {
        set_error(error, "Layer effect asset is missing or unavailable"); return std::nullopt;
    }
    RenderLayerEffectProfile profile;
    if (!profile.from_json(FileSystem::read_text((database.project_root() / record->source_path).string()), error)) return std::nullopt;
    return profile;
}

std::optional<RenderLayerEffectProfile> RenderLayerEffectProfile::load_asset_for_authoring(
    AssetHandle handle, const AssetDatabase& database, std::string* error
) {
    const AssetRecord* record = database.find(handle);
    if (!record || (record->status != AssetStatus::Ready && record->status != AssetStatus::MissingDependency) ||
        record->type != AssetType::RenderLayerEffectProfile) {
        set_error(error, "Layer effect source is missing or unavailable"); return std::nullopt;
    }
    RenderLayerEffectProfile profile;
    if (!profile.from_json(FileSystem::read_text((database.project_root() / record->source_path).string()), error)) return std::nullopt;
    return profile;
}

std::vector<std::shared_ptr<ILocalEffectPass>> RenderLayerEffectProfile::build_runtime_passes(
    const AssetDatabase& database, const std::shared_ptr<IMaterialBackend>& backend, std::string* error
) const {
    std::vector<std::shared_ptr<ILocalEffectPass>> result;
    for (const auto& description : passes) {
        if (!description.enabled) continue;
        auto material = MaterialAsset::load(description.material_handle, database);
        if (!material) { set_error(error, "Layer effect material is missing or invalid"); return {}; }
        result.push_back(std::make_shared<MaterialLocalEffectPass>(std::move(material), backend));
    }
    return result;
}

} // namespace Engine
