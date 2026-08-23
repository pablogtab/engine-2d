#pragma once

#include <string>
#include <vector>
#include <memory>
#include <format>

namespace EngineScript {

enum class TypeKind {
    Void,
    Bool,
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Float32,
    Float64,
    String,
    Vec2,
    Vec3,
    Vec4,
    Color,
    Duration,
    Entity,
    Class,
    Struct,
    Interface,
    Enum,
    Array,
    FixedArray,
    Map,
    Set,
    Optional,
    Result,
    Task,
    Function,
    Null,
    Error
};

struct Type : public std::enable_shared_from_this<Type> {
    TypeKind kind = TypeKind::Error;
    std::string name;
    std::vector<std::shared_ptr<Type>> type_args;
    std::shared_ptr<Type> return_type; // For functions
    std::vector<std::shared_ptr<Type>> param_types; // For functions
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

        if (kind == target->kind) {
            if (kind == TypeKind::Class || kind == TypeKind::Struct || kind == TypeKind::Interface || kind == TypeKind::Enum) {
                return name == target->name;
            }
            if (kind == TypeKind::Array || kind == TypeKind::Set || kind == TypeKind::Task || kind == TypeKind::Optional) {
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
            return true;
        }

        // Numeric implicit widening (Int32 -> Float32 / Float64, Float32 -> Float64)
        if (is_numeric() && target->is_numeric()) {
            if (kind == TypeKind::Int32 && (target->kind == TypeKind::Float32 || target->kind == TypeKind::Float64)) return true;
            if (kind == TypeKind::Float32 && target->kind == TypeKind::Float64) return true;
        }

        return false;
    }

    std::string to_string() const {
        std::string s;
        switch (kind) {
            case TypeKind::Void:       s = "Void"; break;
            case TypeKind::Bool:       s = "Bool"; break;
            case TypeKind::Int8:       s = "Int8"; break;
            case TypeKind::Int16:      s = "Int16"; break;
            case TypeKind::Int32:      s = "Int"; break;
            case TypeKind::Int64:      s = "Int64"; break;
            case TypeKind::UInt8:      s = "UInt8"; break;
            case TypeKind::UInt16:     s = "UInt16"; break;
            case TypeKind::UInt32:     s = "UInt"; break;
            case TypeKind::UInt64:     s = "UInt64"; break;
            case TypeKind::Float32:    s = "Float"; break;
            case TypeKind::Float64:    s = "Float64"; break;
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

        if (!type_args.empty()) {
            s += "<";
            for (size_t i = 0; i < type_args.size(); ++i) {
                s += type_args[i]->to_string();
                if (i + 1 < type_args.size()) s += ", ";
            }
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
