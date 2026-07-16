#include <algorithm>
#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view version = "1.0.0-dev";

void print_usage(std::ostream& output) {
    output
        << "Usage: oof <dump|build|validate|diff|edit> [options]\n"
        << "       oof --version\n";
}

bool requests_json(int argc, char** argv) {
    return std::any_of(argv + 1, argv + argc, [](const char* argument) {
        return std::string_view(argument) == "--json";
    });
}

int unavailable_command(std::string_view command, bool json) {
    if (json) {
        std::cout
            << "{\"ok\":false,\"diagnostics\":[{"
            << "\"code\":\"OOF0001\","
            << "\"severity\":\"error\","
            << "\"path\":\"\","
            << "\"objectId\":\"\","
            << "\"property\":\"\","
            << "\"expected\":\"implemented product adapter\","
            << "\"actual\":\"migration build\","
            << "\"message\":\"Command " << command
            << " is not connected to the product adapters yet\"}]}\n";
    } else {
        std::cerr << "oof: command '" << command
                  << "' is not connected to the product adapters yet\n";
    }
    return 78;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "oof " << version << '\n';
        return 0;
    }
    if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--help")) {
        print_usage(std::cout);
        return argc == 1 ? 64 : 0;
    }

    const std::string_view command = argv[1];
    if (command == "dump" || command == "build" || command == "validate" ||
        command == "diff" || command == "edit") {
        return unavailable_command(command, requests_json(argc, argv));
    }

    if (requests_json(argc, argv)) {
        std::cout
            << "{\"ok\":false,\"diagnostics\":[{"
            << "\"code\":\"OOF0002\","
            << "\"severity\":\"error\","
            << "\"path\":\"\","
            << "\"objectId\":\"\","
            << "\"property\":\"\","
            << "\"expected\":\"dump|build|validate|diff|edit\","
            << "\"actual\":\"" << command << "\","
            << "\"message\":\"Unknown command\"}]}\n";
    } else {
        std::cerr << "oof: unknown command '" << command << "'\n";
        print_usage(std::cerr);
    }
    return 64;
}
