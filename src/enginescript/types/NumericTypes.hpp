#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

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
    Rect,
    Bounds2,
    Duration,
    Entity,
    Class,
    Struct,
    Interface,
    Enum,
    Array,
    Deque,
    FixedArray,
    Map,
    Set,
    Optional,
    Result,
    Task,
    Weak,
    TaskScope,
    Function,
    TypeParameter,
    Null,
    Error
};

struct NumericTypeInfo {
    TypeKind kind;
    std::string_view canonical_name;
    std::string_view display_name;
    std::string_view cpp_name;
    std::string_view range_description;
    uint8_t bit_width;
    bool is_integer;
    bool is_signed;
};

inline constexpr std::array<NumericTypeInfo, 10> NUMERIC_TYPES{{
    {TypeKind::Int8, "Int8", "Int8", "int8_t", "-128...127", 8, true, true},
    {TypeKind::Int16, "Int16", "Int16", "int16_t", "-32768...32767", 16, true, true},
    {TypeKind::Int32, "Int32", "Int", "int32_t", "-2147483648...2147483647", 32, true, true},
    {TypeKind::Int64, "Int64", "Int64", "int64_t", "-9223372036854775808...9223372036854775807", 64, true, true},
    {TypeKind::UInt8, "UInt8", "UInt8", "uint8_t", "0...255", 8, true, false},
    {TypeKind::UInt16, "UInt16", "UInt16", "uint16_t", "0...65535", 16, true, false},
    {TypeKind::UInt32, "UInt32", "UInt", "uint32_t", "0...4294967295", 32, true, false},
    {TypeKind::UInt64, "UInt64", "UInt64", "uint64_t", "0...18446744073709551615", 64, true, false},
    {TypeKind::Float32, "Float32", "Float", "float", "IEEE-754 binary32", 32, false, true},
    {TypeKind::Float64, "Float64", "Float64", "double", "IEEE-754 binary64", 64, false, true},
}};

[[nodiscard]] inline constexpr const NumericTypeInfo* numeric_type_info(TypeKind kind) {
    for (const NumericTypeInfo& info : NUMERIC_TYPES) {
        if (info.kind == kind) return &info;
    }
    return nullptr;
}

[[nodiscard]] inline constexpr std::optional<TypeKind> numeric_type_for_name(std::string_view name) {
    if (name == "Int") return TypeKind::Int32;
    if (name == "UInt") return TypeKind::UInt32;
    if (name == "Float") return TypeKind::Float32;
    for (const NumericTypeInfo& info : NUMERIC_TYPES) {
        if (info.canonical_name == name) return info.kind;
    }
    return std::nullopt;
}

[[nodiscard]] inline constexpr bool is_implicit_numeric_conversion(TypeKind source, TypeKind target) {
    if (source == target) return numeric_type_info(source) != nullptr;
    const NumericTypeInfo* from = numeric_type_info(source);
    const NumericTypeInfo* to = numeric_type_info(target);
    if (!from || !to) return false;

    if (from->is_integer && to->is_integer) {
        if (from->is_signed == to->is_signed) return from->bit_width < to->bit_width;
        return !from->is_signed && to->is_signed && from->bit_width < to->bit_width;
    }
    if (from->is_integer && !to->is_integer) {
        if (to->bit_width == 32) {
            return (from->is_signed && from->bit_width <= 32) ||
                (!from->is_signed && from->bit_width <= 16);
        }
        return to->bit_width == 64 && from->bit_width <= 32;
    }
    return !from->is_integer && !to->is_integer &&
        from->bit_width == 32 && to->bit_width == 64;
}

[[nodiscard]] inline constexpr std::optional<TypeKind> promote_numeric_types(TypeKind left, TypeKind right) {
    if (!numeric_type_info(left) || !numeric_type_info(right)) return std::nullopt;
    if (left == right) return left;
    if (is_implicit_numeric_conversion(left, right)) return right;
    if (is_implicit_numeric_conversion(right, left)) return left;
    return std::nullopt;
}

[[nodiscard]] inline constexpr std::optional<std::size_t> value_constructor_width(std::string_view name) {
    if (name == "Vec2") return 2;
    if (name == "Vec3") return 3;
    if (name == "Vec4" || name == "Color") return 4;
    if (name == "Rect" || name == "Bounds2") return 4;
    return std::nullopt;
}

} // namespace EngineScript
