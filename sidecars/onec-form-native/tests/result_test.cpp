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

        oof::Diagnostic diagnostic;
        diagnostic.code = "OOF_TEST";
        diagnostic.message = "failure";
        auto failure = oof::Result<std::string>::failure({diagnostic});
        expect(!failure.ok(), "failure result must be unsuccessful");
        expect(failure.diagnostics().size() == 1, "failure must retain diagnostics");
        expect(failure.diagnostics().front().code == "OOF_TEST", "diagnostic code must survive");

        auto void_success = oof::Result<void>::success();
        expect(void_success.ok(), "void success must be successful");
        auto void_failure = oof::Result<void>::failure({diagnostic});
        expect(!void_failure.ok(), "void failure must be unsuccessful");
    } catch (const std::exception& error) {
        std::cerr << "result tests: FAIL: " << error.what() << '\n';
        return 1;
    }

    std::cout << "result tests: PASS\n";
    return 0;
}
