#include "MetadataEmitter.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <optional>
#include <unordered_map>

namespace EngineScript {

using json = nlohmann::ordered_json;

namespace {

std::optional<double> constant_number(const std::shared_ptr<IROperation>& operation) {
    if (!operation) return std::nullopt;
    if (operation->kind == IROpKind::Constant) {
        const auto constant = std::static_pointer_cast<IRConstant>(operation);
        if (!constant->result_type || !constant->result_type->is_numeric()) return std::nullopt;
        return constant->result_type->is_integer()
            ? static_cast<double>(constant->int_val)
            : constant->float_val;
    }
    if (operation->kind == IROpKind::Unary) {
        const auto unary = std::static_pointer_cast<IRUnary>(operation);
        const auto operand = constant_number(unary->operand);
        if (!operand) return std::nullopt;
        if (unary->op == TokenType::Minus) return -*operand;
        if (unary->op == TokenType::Plus) return *operand;
    }
    if (operation->kind == IROpKind::NumericConvert) {
        return constant_number(std::static_pointer_cast<IRNumericConvert>(operation)->operand);
    }
    return std::nullopt;
}

std::optional<std::string> vector2_default(const std::shared_ptr<IROperation>& operation) {
    if (!operation || operation->kind != IROpKind::ValueConstruct) return std::nullopt;
    const auto construction = std::static_pointer_cast<IRValueConstruct>(operation);
    if (construction->target_kind != TypeKind::Vec2 || construction->args.size() != 2) return std::nullopt;
    const auto x = constant_number(construction->args[0]);
    const auto y = constant_number(construction->args[1]);
    if (!x || !y) return std::nullopt;
    return json::array({*x, *y}).dump();
}

} // namespace

std::string MetadataEmitter::emit_json(const std::vector<IRModule>& modules) {
    json root;
    root["sdkHash"] = ENGINE_SCRIPT_SDK_HASH;
    root["languageVersion"] = ENGINE_SCRIPT_LANGUAGE_VERSION;
    root["compilerCacheId"] = ENGINE_SCRIPT_COMPILER_CACHE_ID;

    json behaviours_arr = json::array();
    json structs_arr = json::array();
    json enums_arr = json::array();

    std::unordered_map<std::string, std::vector<std::string>> enum_members;
    for (const auto& mod : modules) {
        for (const auto& enum_decl : mod.enums) {
            std::vector<std::string> members;
            for (const auto& entry : enum_decl.entries) members.push_back(entry.name);
            enum_members.emplace(enum_decl.name, members);
            enums_arr.push_back({{"name", enum_decl.name}, {"members", std::move(members)}});
        }
    }

    for (const auto& mod : modules) {
        for (const auto& cls : mod.classes) {
            if (cls.is_behaviour) {
                json b_obj;
                b_obj["name"] = cls.name;
                b_obj["module"] = mod.module_path;

                json deps_arr = json::array();
                for (const auto& p : cls.constructor_params) {
                    json dep_obj;
                    dep_obj["name"] = p.name;
                    dep_obj["type"] = p.type ? p.type->to_string() : "unknown";

                    bool is_inject = false;
                    bool is_comp = false;
                    bool is_opt = false;
                    for (const auto& d : p.decorators) {
                        if (d == "inject") is_inject = true;
                        if (d == "component") is_comp = true;
                        if (d == "optional") is_opt = true;
                    }
                    dep_obj["isService"] = is_inject;
                    dep_obj["isComponent"] = is_comp;
                    dep_obj["isOptional"] = is_opt;
                    deps_arr.push_back(dep_obj);
                }
                b_obj["dependencies"] = deps_arr;

                json props_arr = json::array();
                for (const auto& prop : cls.properties) {
                    json p_obj;
                    p_obj["name"] = prop.name;
                    p_obj["type"] = prop.type ? prop.type->to_string() : "Float";

                    bool is_exp = false;
                    bool is_ser = true;
                    for (const auto& d : prop.decorators) {
                        if (d == "expose") is_exp = true;
                        if (d == "serializable") is_ser = true;
                    }
                    p_obj["isExposed"] = is_exp;
                    p_obj["isSerializable"] = is_ser;
                    p_obj["isBehaviourReference"] = prop.type && prop.type->is_behaviour;
                    if (prop.type && prop.type->kind == TypeKind::Enum) {
                        p_obj["enumType"] = prop.type->name;
                        p_obj["enumValues"] = enum_members[prop.type->name];
                    }

                    auto min_it = prop.numeric_attributes.find("min");
                    if (min_it != prop.numeric_attributes.end()) {
                        p_obj["minValue"] = min_it->second;
                    }
                    auto max_it = prop.numeric_attributes.find("max");
                    if (max_it != prop.numeric_attributes.end()) {
                        p_obj["maxValue"] = max_it->second;
                    }
                    if (prop.initializer && prop.initializer->kind == IROpKind::Constant) {
                        auto const_op = std::static_pointer_cast<IRConstant>(prop.initializer);
                        if (const_op->result_type && const_op->result_type->is_string()) {
                            p_obj["defaultValue"] = const_op->string_val;
                        } else if (const_op->result_type && const_op->result_type->is_bool()) {
                            p_obj["defaultValue"] = const_op->bool_val ? "true" : "false";
                        } else if (const_op->result_type && const_op->result_type->is_integer()) {
                            p_obj["defaultValue"] = std::to_string(const_op->int_val);
                        } else {
                            p_obj["defaultValue"] = std::to_string(const_op->float_val);
                        }
                    } else if (const auto value = vector2_default(prop.initializer)) {
                        p_obj["defaultValue"] = *value;
                    } else if (prop.initializer && prop.initializer->kind == IROpKind::MemberGet) {
                        const auto member = std::static_pointer_cast<IRMemberGet>(prop.initializer);
                        if (member->result_type && member->result_type->kind == TypeKind::Enum) {
                            p_obj["defaultValue"] = member->member_name;
                        }
                    }

                    props_arr.push_back(p_obj);
                }
                b_obj["properties"] = props_arr;

                json methods_arr = json::array();
                for (const auto& m : cls.methods) {
                    methods_arr.push_back(m.name);
                }
                b_obj["methods"] = methods_arr;

                behaviours_arr.push_back(b_obj);
            }
        }

        for (const auto& st : mod.structs) {
            structs_arr.push_back(st.name);
        }
    }

    root["behaviours"] = behaviours_arr;
    root["structs"] = structs_arr;
    root["enums"] = enums_arr;
    return root.dump(2);
}

ProjectMetadataManifest MetadataEmitter::parse_json(const std::string& json_str) {
    ProjectMetadataManifest manifest;
    try {
        json root = json::parse(json_str);
        if (root.contains("languageVersion")) manifest.language_version = root["languageVersion"].get<std::uint32_t>();
        if (root.contains("compilerCacheId")) manifest.compiler_cache_id = root["compilerCacheId"].get<std::string>();
        if (root.contains("sdkHash")) manifest.sdk_hash = root["sdkHash"].get<std::string>();

        if (root.contains("behaviours") && root["behaviours"].is_array()) {
            for (const auto& b : root["behaviours"]) {
                BehaviourManifest bm;
                if (b.contains("name")) bm.name = b["name"].get<std::string>();
                if (b.contains("module")) bm.module_path = b["module"].get<std::string>();

                if (b.contains("dependencies") && b["dependencies"].is_array()) {
                    for (const auto& dep : b["dependencies"]) {
                        InjectedDependencyDesc dd;
                        if (dep.contains("name")) dd.name = dep["name"].get<std::string>();
                        if (dep.contains("type")) dd.type = dep["type"].get<std::string>();
                        if (dep.contains("isService")) dd.is_service = dep["isService"].get<bool>();
                        if (dep.contains("isComponent")) dd.is_component = dep["isComponent"].get<bool>();
                        if (dep.contains("isOptional")) dd.is_optional = dep["isOptional"].get<bool>();
                        bm.dependencies.push_back(dd);
                    }
                }

                if (b.contains("properties") && b["properties"].is_array()) {
                    for (const auto& prop : b["properties"]) {
                        ExposedPropertyDesc epd;
                        if (prop.contains("name")) epd.name = prop["name"].get<std::string>();
                        if (prop.contains("type")) epd.type = prop["type"].get<std::string>();
                        if (prop.contains("defaultValue")) epd.default_value_str = prop["defaultValue"].get<std::string>();
                        if (prop.contains("isExposed")) epd.is_exposed = prop["isExposed"].get<bool>();
                        if (prop.contains("isSerializable")) epd.is_serializable = prop["isSerializable"].get<bool>();
                        if (prop.contains("isBehaviourReference")) epd.is_behaviour_reference = prop["isBehaviourReference"].get<bool>();
                        if (prop.contains("enumType")) epd.enum_type = prop["enumType"].get<std::string>();
                        if (prop.contains("enumValues") && prop["enumValues"].is_array()) {
                            for (const auto& value : prop["enumValues"]) epd.enum_values.push_back(value.get<std::string>());
                        }
                        if (prop.contains("minValue")) epd.min_value = prop["minValue"].get<double>();
                        if (prop.contains("maxValue")) epd.max_value = prop["maxValue"].get<double>();
                        bm.properties.push_back(epd);
                    }
                }

                if (b.contains("methods") && b["methods"].is_array()) {
                    for (const auto& m : b["methods"]) {
                        bm.methods.push_back(m.get<std::string>());
                    }
                }

                manifest.behaviours.push_back(bm);
            }
        }

        if (root.contains("structs") && root["structs"].is_array()) {
            for (const auto& s : root["structs"]) {
                manifest.structs.push_back(s.get<std::string>());
            }
        }
        if (root.contains("enums") && root["enums"].is_array()) {
            for (const auto& value : root["enums"]) {
                if (!value.is_object()) continue;
                EnumManifest enum_manifest;
                enum_manifest.name = value.value("name", "");
                if (value.contains("members") && value["members"].is_array()) {
                    for (const auto& member : value["members"]) enum_manifest.members.push_back(member.get<std::string>());
                }
                manifest.enums.push_back(std::move(enum_manifest));
            }
        }
    } catch (...) {
        // Return whatever was parsed
    }
    return manifest;
}

} // namespace EngineScript
