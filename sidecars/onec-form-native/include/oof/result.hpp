#pragma once

#include <stdexcept>
#include <utility>
#include <variant>

#include "oof/diagnostic.hpp"

namespace oof {

template <typename T>
class Result {
public:
    static Result success(T value, Diagnostics diagnostics = {}) {
        return Result(Success{std::move(value), std::move(diagnostics)});
    }

    static Result failure(Diagnostics diagnostics) {
        return Result(std::move(diagnostics));
    }

    bool ok() const noexcept {
        return std::holds_alternative<Success>(state_);
    }

    explicit operator bool() const noexcept {
        return ok();
    }

    const T& value() const {
        if (!ok()) {
            throw std::logic_error("result has no value");
        }
        return std::get<Success>(state_).value;
    }

    T& value() {
        if (!ok()) {
            throw std::logic_error("result has no value");
        }
        return std::get<Success>(state_).value;
    }

    T take_value() {
        if (!ok()) {
            throw std::logic_error("result has no value");
        }
        return std::move(std::get<Success>(state_).value);
    }

    const Diagnostics& diagnostics() const noexcept {
        return ok() ? std::get<Success>(state_).diagnostics : std::get<Diagnostics>(state_);
    }

private:
    struct Success {
        T value;
        Diagnostics diagnostics;
    };

    explicit Result(Success success)
        : state_(std::move(success)) {}

    explicit Result(Diagnostics diagnostics)
        : state_(std::move(diagnostics)) {}

    std::variant<Success, Diagnostics> state_;
};

template <>
class Result<void> {
public:
    static Result success(Diagnostics diagnostics = {}) {
        return Result(true, std::move(diagnostics));
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
