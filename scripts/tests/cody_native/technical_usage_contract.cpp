// Actual firmware numeric parsers; no driver, network or synthetic firmware publication.
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "technical_usage_under_test.inc"

// condition: Required outcome from the exact firmware parser.
// message: Original failing boundary retained in the host error output.
// Exception: Terminates this host fixture on a failed assertion.
void require(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// No parameters: fixed decimal boundaries exercise production code rather than a replica.
// Exception: Returns nonzero only through a failed contract assertion.
int main()
{
    bool present = true;
    uint64_t value = 99;
    require(parseTechnicalCount("-", present, value) && !present, "absent count has no measurement");
    require(parseTechnicalCount("0", present, value) && present && value == 0, "zero is a genuine measured value");
    require(parseTechnicalCount("18446744073709551615", present, value) && present && value == UINT64_MAX,
        "maximum uint64 remains exact");
    const char *invalid[] = {"", "00", "01", "-1", "+1", " 1", "1 ", "1.0", "1e3", "18446744073709551616"};
    for (const char *text : invalid) {
        require(!parseTechnicalCount(text, present, value), "noncanonical or overflowing count is rejected");
    }
    require(parseTechnicalDuration("4294967295", present, value) && value == UINT64_C(4294967295000),
        "original uint32 source age converts without overflow");
    require(!parseTechnicalDuration("4294967296", present, value), "overflowing source age is rejected");
    require(parseTechnicalDuration("0", present, value) && present && value == 0, "zero source age remains present");
    require(parseTechnicalDuration("-", present, value) && !present, "absent source age remains absent");
    return 0;
}
