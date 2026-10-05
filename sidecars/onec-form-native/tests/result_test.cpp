#include <iostream>
#include <stdexcept>
#include <string>

#include "oof/result.hpp"

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int main() {
    try {
        auto success = oof::Result<std::string>::success("value");
        expect(success.ok(), "success result must be successful");
        expect(success.value() == "value", "success result must expose its value");
        expect(success.diagnostics().empty(), "success result must not expose diagnostics");

        oof::Diagnostic warning;
        warning.code = "OOF_TEST_WARNING";
        warning.severity = oof::DiagnosticSeverity::warning;
        warning.message = "partial reconstruction";
        auto partial = oof::Result<std::string>::success("partial", {warning});
        expect(partial.ok(), "success with warning must remain successful");
        expect(partial.value() == "partial", "success with warning must expose its value");
        expect(partial.diagnostics().size() == 1 &&
                   partial.diagnostics().front().severity == oof::DiagnosticSeverity::warning,
            "success with warning must retain diagnostics");

        oof::Diagnostic diagnostic;
        diagnostic.code = "OOF_TEST";
        diagnostic.message = "failure";
        auto failure = oof::Result<std::string>::failure({diagnostic});
        expect(!failure.ok(), "failure result must be unsuccessful");
        expect(failure.diagnostics().size() == 1, "failure must retain diagnostics");
        expect(failure.diagnostics().front().code == "OOF_TEST", "diagnostic code must survive");

        auto void_success = oof::Result<void>::success();
        expect(void_success.ok(), "void success must be successful");
        auto void_warning = oof::Result<void>::success({warning});
        expect(void_warning.ok() && void_warning.diagnostics().size() == 1,
            "void success must retain warnings");
        auto void_failure = oof::Result<void>::failure({diagnostic});
        expect(!void_failure.ok(), "void failure must be unsuccessful");
    } catch (const std::exception& error) {
        std::cerr << "result tests: FAIL: " << error.what() << '\n';
        return 1;
    }

    std::cout << "result tests: PASS\n";
    return 0;
}
