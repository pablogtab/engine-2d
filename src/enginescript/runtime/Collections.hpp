#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace EngineScript {

class ScriptRuntimeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

template <typename Index>
[[nodiscard]] std::size_t checkedCollectionIndex(Index index, std::size_t size) {
    static_assert(std::is_integral_v<Index>);
    if constexpr (std::is_signed_v<Index>) {
        if (index < 0) throw ScriptRuntimeError("collection index is out of range");
    }
    const auto position = static_cast<std::size_t>(index);
    if (position >= size) throw ScriptRuntimeError("collection index is out of range");
    return position;
}

template <typename T, typename Allocator, typename Index>
T& checkedIndex(std::vector<T, Allocator>& values, Index index) {
    return values[checkedCollectionIndex(index, values.size())];
}

template <typename T, typename Allocator, typename Index>
const T& checkedIndex(const std::vector<T, Allocator>& values, Index index) {
    return values[checkedCollectionIndex(index, values.size())];
}

template <typename T, std::size_t Size, typename Index>
T& checkedIndex(std::array<T, Size>& values, Index index) {
    return values[checkedCollectionIndex(index, Size)];
}

template <typename T, std::size_t Size, typename Index>
const T& checkedIndex(const std::array<T, Size>& values, Index index) {
    return values[checkedCollectionIndex(index, Size)];
}

template <typename T, typename Allocator>
[[nodiscard]] bool containsValue(const std::vector<T, Allocator>& values, const T& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename T, typename Allocator>
[[nodiscard]] bool removeValue(std::vector<T, Allocator>& values, const T& value) {
    const auto found = std::find(values.begin(), values.end(), value);
    if (found == values.end()) return false;
    values.erase(found);
    return true;
}

template <typename T, typename Allocator>
[[nodiscard]] std::optional<T> popValue(std::vector<T, Allocator>& values) {
    if (values.empty()) return std::nullopt;
    T value = std::move(values.back());
    values.pop_back();
    return value;
}

template <typename Key, typename Value, typename Compare, typename Allocator, typename KeyLike>
requires std::is_constructible_v<Key, KeyLike>
std::optional<Value> checkedLookup(
    const std::map<Key, Value, Compare, Allocator>& values,
    KeyLike&& key
) {
    const auto found = values.find(Key(std::forward<KeyLike>(key)));
    return found == values.end() ? std::nullopt : std::optional<Value>{found->second};
}

template <typename Key, typename Value, typename Compare, typename Allocator>
[[nodiscard]] std::vector<Key> mapKeys(const std::map<Key, Value, Compare, Allocator>& values) {
    std::vector<Key> result;
    result.reserve(values.size());
    for (const auto& [key, value] : values) {
        static_cast<void>(value);
        result.push_back(key);
    }
    return result;
}

template <typename Key, typename Value, typename Compare, typename Allocator>
[[nodiscard]] std::vector<Value> mapValues(const std::map<Key, Value, Compare, Allocator>& values) {
    std::vector<Value> result;
    result.reserve(values.size());
    for (const auto& [key, value] : values) {
        static_cast<void>(key);
        result.push_back(value);
    }
    return result;
}

} // namespace EngineScript
