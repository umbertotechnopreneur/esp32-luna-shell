#include "LUNASHELL_codex_events.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct GuardedPayload {
    uint64_t before = 0x12345678abcdef90ULL;
    LUNASHELL::CodexEventPayload payload{};
    uint64_t after = 0x9876543210abcdefULL;
};

// hex: Host-encoded payload or deliberately malformed wire data.
// expected_valid: Independent fixture verdict, not inferred from this decoder.
// Exception: Throws a descriptive fixture failure without treating partial output as valid.
void check_payload(const std::string &hex, bool expected_valid)
{
    GuardedPayload guarded{};
    const esp_err_t result = LUNASHELL::decodeCodexEventPayload(hex.data(), hex.size(), guarded.payload);
    if (guarded.before != 0x12345678abcdef90ULL || guarded.after != 0x9876543210abcdefULL) {
        throw std::runtime_error("Payload decoder crossed its output boundary");
    }
    if ((result == ESP_OK) != expected_valid) {
        throw std::runtime_error("Firmware verdict differs from independent host fixture");
    }
    if (!expected_valid) {
        return;
    }
    // Every strict prefix lacks part of the declared message, including the
    // final progress word. No truncated UDP body may become a valid snapshot.
    for (size_t length = 0; length < hex.size(); ++length) {
        if (LUNASHELL::decodeCodexEventPayload(hex.data(), length, guarded.payload) == ESP_OK) {
            throw std::runtime_error("Truncated payload was accepted");
        }
    }
    const std::string trailing = hex + "00";
    if (LUNASHELL::decodeCodexEventPayload(trailing.data(), trailing.size(), guarded.payload) == ESP_OK) {
        throw std::runtime_error("Trailing payload byte was accepted");
    }
}
} // namespace

// argc: Must contain exactly one fixture-file argument.
// argv: Executable name and generated fixture path under artifacts/checks/.
// Exception: Converts fixture/file errors into a failed process exit with the line number.
int main(int argc, char **argv)
{
    size_t line_number = 0;
    try {
        if (!LUNASHELL::validCodexResetCode("043533477") ||
            LUNASHELL::validCodexResetCode(nullptr) || LUNASHELL::validCodexResetCode("") ||
            LUNASHELL::validCodexResetCode("43533477") || LUNASHELL::validCodexResetCode("043533478") ||
            LUNASHELL::validCodexResetCode("043533477 ") || LUNASHELL::validCodexResetCode(" 043533477")) {
            throw std::runtime_error("Reset requires exactly the human confirmation code, including its leading zero");
        }
        if (argc != 2) {
            throw std::runtime_error("Expected the cross-language fixture file");
        }
        std::ifstream input(argv[1]);
        if (!input) {
            throw std::runtime_error("Cannot open cross-language fixture file");
        }
        std::string line;
        while (std::getline(input, line)) {
            ++line_number;
            if (line.size() < 2 || line[1] != '\t' || (line[0] != '0' && line[0] != '1')) {
                throw std::runtime_error("Invalid fixture record");
            }
            check_payload(line.substr(2), line[0] == '1');
        }
        if (!input.eof() || line_number == 0) {
            throw std::runtime_error("Incomplete or empty fixture file");
        }
        std::cout << "CCP native payload fixtures passed: " << line_number << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "CCP native fixture " << line_number << ": " << error.what() << '\n';
        return 1;
    }
}
