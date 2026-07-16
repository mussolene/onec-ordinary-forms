#pragma once

#include <stdexcept>
#include <utility>
#include <variant>

#include "oof/diagnostic.hpp"

namespace oof {

template <typename T>
class Result {
public:
    static Result success(T value) {
        return Result(std::move(value));
    }

    static Result failure(Diagnostics diagnostics) {
        return Result(std::move(diagnostics));
    }

    bool ok() const noexcept {
        return std::holds_alternative<T>(state_);
    }

    explicit operator bool() const noexcept {
        return ok();
    }

    const T& value() const {
        if (!ok()) {
            throw std::logic_error("result has no value");
        }
        return std::get<T>(state_);
    }

    T& value() {
        if (!ok()) {
            throw std::logic_error("result has no value");
        }
        return std::get<T>(state_);
    }

    T take_value() {
        if (!ok()) {
            throw std::logic_error("result has no value");
        }
        return std::move(std::get<T>(state_));
    }

    const Diagnostics& diagnostics() const noexcept {
        static const Diagnostics empty;
        return ok() ? empty : std::get<Diagnostics>(state_);
    }

private:
    explicit Result(T value)
        : state_(std::move(value)) {}

    explicit Result(Diagnostics diagnostics)
        : state_(std::move(diagnostics)) {}

    std::variant<T, Diagnostics> state_;
};

template <>
class Result<void> {
public:
    static Result success() {
        return Result(true, {});
    }

    static Result failure(Diagnostics diagnostics) {
        return Result(false, std::move(diagnostics));
    }

    bool ok() const noexcept {
        return ok_;
    }

    explicit operator bool() const noexcept {
        return ok();
    }

    const Diagnostics& diagnostics() const noexcept {
        return diagnostics_;
    }

private:
    Result(bool ok, Diagnostics diagnostics)
        : ok_(ok), diagnostics_(std::move(diagnostics)) {}

    bool ok_;
    Diagnostics diagnostics_;
};

}  // namespace oof
