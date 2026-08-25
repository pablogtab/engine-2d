#pragma once

#include <optional>
#include <string>
#include <utility>

namespace EngineScript {

template <typename T, typename E = std::string>
class Result {
public:
    static Result success(T value) {
        Result result;
        result.m_value = std::move(value);
        return result;
    }

    static Result failure(E error) {
        Result result;
        result.m_error = std::move(error);
        return result;
    }

    [[nodiscard]] bool isOk() const { return m_value.has_value(); }
    [[nodiscard]] bool isError() const { return !isOk(); }
    [[nodiscard]] T value() const { return m_value.value_or(T{}); }
    [[nodiscard]] E error() const { return m_error.value_or(E{}); }

private:
    std::optional<T> m_value;
    std::optional<E> m_error;
};

template <typename E>
class Result<void, E> {
public:
    static Result success() { return Result{}; }
    static Result failure(E error) {
        Result result;
        result.m_error = std::move(error);
        return result;
    }

    [[nodiscard]] bool isOk() const { return !m_error.has_value(); }
    [[nodiscard]] bool isError() const { return !isOk(); }
    [[nodiscard]] E error() const { return m_error.value_or(E{}); }

private:
    std::optional<E> m_error;
};

} // namespace EngineScript
