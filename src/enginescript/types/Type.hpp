#pragma once

#include "NumericTypes.hpp"

#include <string>
#include <vector>
#include <memory>
#include <format>
#include <algorithm>
#include <optional>

namespace EngineScript {

struct Type : public std::enable_shared_from_this<Type> {
    TypeKind kind = TypeKind::Error;
    std::string name;
    std::vector<std::shared_ptr<Type>> type_args;
    std::shared_ptr<Type> return_type; // For functions
    std::vector<std::shared_ptr<Type>> param_types; // For functions
    std::optional<std::size_t> fixed_array_size;
    std::vector<std::string> interfaces;
    bool is_behaviour = false;
    bool is_nullable = false;

    bool is_numeric() const {
        return is_integer() || is_float();
    }

    bool is_integer() const {
        return kind >= TypeKind::Int8 && kind <= TypeKind::UInt64;
    }

    bool is_float() const {
        return kind == TypeKind::Float32 || kind == TypeKind::Float64;
    }

    bool is_void() const { return kind == TypeKind::Void; }
    bool is_bool() const { return kind == TypeKind::Bool; }
    bool is_string() const { return kind == TypeKind::String; }
    bool is_null() const { return kind == TypeKind::Null; }
    bool is_error() const { return kind == TypeKind::Error; }

    bool is_assignable_to(const std::shared_ptr<Type>& target) const {
        if (!target) return false;
        if (kind == TypeKind::Error || target->kind == TypeKind::Error) return true; // Suppress cascade errors

        // Null assigning to nullable
        if (kind == TypeKind::Null) {
            return target->is_nullable || target->kind == TypeKind::Null;
        }

        // Nullability constraint
        if (is_nullable && !target->is_nullable) {
            return false;
        }

        const auto is_nominal = [](TypeKind candidate) {
            return candidate == TypeKind::Class || candidate == TypeKind::Struct ||
                candidate == TypeKind::Interface || candidate == TypeKind::Enum;
        };
        if (is_nominal(kind) && is_nominal(target->kind) && kind == target->kind && name == target->name) {
            if (target->type_args.empty()) return true;
            if (type_args.size() != target->type_args.size()) return false;
            for (std::size_t index = 0; index < type_args.size(); ++index) {
                if (!type_args[index]->is_assignable_to(target->type_args[index])) return false;
            }
            return true;
        }

        if (kind == target->kind) {
            if (kind == TypeKind::Array || kind == TypeKind::Set || kind == TypeKind::Task ||
                kind == TypeKind::Optional || kind == TypeKind::Weak) {
                if (type_args.size() == target->type_args.size() && !type_args.empty()) {
                    return type_args[0]->is_assignable_to(target->type_args[0]);
                }
            }
            if (kind == TypeKind::Map || kind == TypeKind::Result) {
                if (type_args.size() == 2 && target->type_args.size() == 2) {
                    return type_args[0]->is_assignable_to(target->type_args[0]) &&
                           type_args[1]->is_assignable_to(target->type_args[1]);
                }
            }
            if (kind == TypeKind::FixedArray) {
                return fixed_array_size == target->fixed_array_size && type_args.size() == 1 &&
                    target->type_args.size() == 1 && type_args[0]->is_assignable_to(target->type_args[0]);
            }
            return true;
        }

        if (is_numeric() && target->is_numeric()) {
            return is_implicit_numeric_conversion(kind, target->kind);
        }

        if (target->kind == TypeKind::Interface &&
            std::find(interfaces.begin(), interfaces.end(), target->name) != interfaces.end()) {
            return true;
        }

        return false;
    }

    std::string to_string() const {
        if (const NumericTypeInfo* numeric = numeric_type_info(kind)) {
            return std::string(numeric->display_name) + (is_nullable ? "?" : "");
        }
        std::string s;
        switch (kind) {
            case TypeKind::Void:       s = "Void"; break;
            case TypeKind::Bool:       s = "Bool"; break;
            case TypeKind::String:     s = "String"; break;
            case TypeKind::Vec2:       s = "Vec2"; break;
            case TypeKind::Vec3:       s = "Vec3"; break;
            case TypeKind::Vec4:       s = "Vec4"; break;
            case TypeKind::Color:      s = "Color"; break;
            case TypeKind::Duration:   s = "Duration"; break;
            case TypeKind::Entity:     s = "Entity"; break;
            case TypeKind::Null:       s = "null"; break;
            case TypeKind::Error:      s = "<type-error>"; break;
            default:                   s = name; break;
        }

        if (!type_args.empty() || (kind == TypeKind::FixedArray && fixed_array_size)) {
            s += "<";
            for (size_t i = 0; i < type_args.size(); ++i) {
                s += type_args[i]->to_string();
                if (i + 1 < type_args.size() || fixed_array_size) s += ", ";
            }
            if (kind == TypeKind::FixedArray && fixed_array_size) s += std::to_string(*fixed_array_size);
            s += ">";
        }

        if (is_nullable) s += "?";
        return s;
    }

    static std::shared_ptr<Type> make_primitive(TypeKind kind) {
        auto t = std::make_shared<Type>();
        t->kind = kind;
        return t;
    }

    static std::shared_ptr<Type> make_custom(TypeKind kind, std::string name) {
        auto t = std::make_shared<Type>();
        t->kind = kind;
        t->name = std::move(name);
        return t;
    }

    static std::shared_ptr<Type> make_generic(TypeKind kind, std::string name, std::vector<std::shared_ptr<Type>> args) {
        auto t = std::make_shared<Type>();
        t->kind = kind;
        t->name = std::move(name);
        t->type_args = std::move(args);
        return t;
    }
};

} // namespace EngineScript
