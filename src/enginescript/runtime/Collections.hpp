#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
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
[[nodiscard]] int indexOfValue(const std::vector<T, Allocator>& values, const T& value) {
    const auto found = std::find(values.begin(), values.end(), value);
    return found == values.end() ? -1 : static_cast<int>(std::distance(values.begin(), found));
}

template <typename T, typename Allocator>
[[nodiscard]] int lastIndexOfValue(const std::vector<T, Allocator>& values, const T& value) {
    const auto found = std::find(values.rbegin(), values.rend(), value);
    return found == values.rend() ? -1 : static_cast<int>(values.size() - 1 - std::distance(values.rbegin(), found));
}

template <typename T, typename Allocator, typename Index>
[[nodiscard]] bool removeAt(std::vector<T, Allocator>& values, Index index) {
    if constexpr (std::is_signed_v<Index>) {
        if (index < 0) return false;
    }
    const auto position = static_cast<std::size_t>(index);
    if (position >= values.size()) return false;
    values.erase(values.begin() + static_cast<std::ptrdiff_t>(position));
    return true;
}

template <typename T, typename Allocator>
void reverseValues(std::vector<T, Allocator>& values) {
    std::reverse(values.begin(), values.end());
}

template <typename T, typename Allocator, typename Index>
void swapValues(std::vector<T, Allocator>& values, Index left, Index right) {
    std::swap(checkedIndex(values, left), checkedIndex(values, right));
}

template <typename T, typename Allocator>
void shuffleValues(std::vector<T, Allocator>& values, std::uint32_t seed) {
    for (std::size_t index = values.size(); index > 1; --index) {
        seed = seed * 1664525u + 1013904223u;
        std::swap(values[index - 1], values[seed % index]);
    }
}

template <typename T, typename Allocator>
[[nodiscard]] std::optional<T> chooseValue(const std::vector<T, Allocator>& values, std::uint32_t seed) {
    if (values.empty()) return std::nullopt;
    seed = seed * 1664525u + 1013904223u;
    return values[seed % values.size()];
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
