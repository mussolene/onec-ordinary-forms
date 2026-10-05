#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace oof {

enum class DiagnosticSeverity {
    info,
    warning,
    error,
};

constexpr std::string_view to_string(DiagnosticSeverity severity) noexcept {
    switch (severity) {
        case DiagnosticSeverity::info:
            return "info";
        case DiagnosticSeverity::warning:
            return "warning";
        case DiagnosticSeverity::error:
            return "error";
    }
    return "error";
}

struct Diagnostic {
    std::string code;
    DiagnosticSeverity severity = DiagnosticSeverity::error;
    std::string object_id;
    std::string path;
    std::string property;
    std::string expected;
    std::string actual;
    std::string message;
};

using Diagnostics = std::vector<Diagnostic>;

}  // namespace oof
