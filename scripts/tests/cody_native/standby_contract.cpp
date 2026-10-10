#include "LUNASHELL_standby_transition.hpp"
#include <array>
#include <cassert>

extern "C" {
esp_err_t cody_host_standby_create(void **output, bool standby);
void cody_host_standby_destroy(void *context);
esp_err_t cody_host_standby_profile(void *context, bool deep, bool retained, bool voice, bool touch);
esp_err_t cody_host_standby_fault(void *context, uint8_t resource, bool target,
    esp_err_t error, uint32_t count, uint32_t skip);
esp_err_t cody_host_standby_transition(void *context, bool target);
esp_err_t cody_host_standby_read(void *context, int32_t *values, size_t count,
    uint8_t *applied, uint8_t *consistent, uint8_t *unsupported, uint32_t *calls, size_t resource_count);
}

namespace {
using LUNASHELL::power_policy::StandbyStep;
struct Observation {
    std::array<int32_t, 12> result{};
    std::array<uint8_t, 15> applied{};
    std::array<uint8_t, 15> consistent{};
    std::array<uint8_t, 15> unsupported{};
    std::array<uint32_t, 15> calls{};
};

// owner: Existing native synthetic-resource owner under test.
// Exception: Assertion failure retains the exact failed ABI operation.
Observation read_owner(void *owner)
{
    Observation observation;
    assert(cody_host_standby_read(owner, observation.result.data(), observation.result.size(),
        observation.applied.data(), observation.consistent.data(), observation.unsupported.data(), observation.calls.data(),
        observation.calls.size()) == ESP_OK);
    return observation;
}

// owner: Existing native synthetic-resource owner under test.
// resource: Typed production resource identity.
// target: Resource policy state whose transport fails.
// error: Original error expected through rollback/recovery.
// count: Failed-call count needed for deterministic repeated recovery failure.
// Exception: Assertion failure identifies actual native fault admission rejection.
void fault(void *owner, StandbyStep resource, bool target, esp_err_t error, uint32_t count = 1)
{
    assert(cody_host_standby_fault(owner, static_cast<uint8_t>(resource), target,
        error, count, 0) == ESP_OK);
}
}

// No parameters: exercises actual firmware resource plans through synthetic native owners.
// Exception: Assertion failure reports a regression; this definition does not run during implementation.
int main()
{
    void *owner = nullptr;
    assert(cody_host_standby_create(&owner, false) == ESP_OK);
    assert(cody_host_standby_profile(owner, true, true, false, true) == ESP_OK);
    assert(cody_host_standby_transition(owner, true) == ESP_OK);
    auto observation = read_owner(owner);
    assert(observation.result[0] == 1 && observation.result[1] == 0);
    assert(observation.result[7] == 0); // Retained notifications require software standby.
    const auto display_calls = observation.calls[static_cast<size_t>(StandbyStep::PrepareDarkFrame)];
    fault(owner, StandbyStep::Distance, false, ESP_ERR_TIMEOUT);
    assert(cody_host_standby_transition(owner, false) == ESP_ERR_TIMEOUT);
    observation = read_owner(owner);
    assert(observation.result[0] == 1 && observation.result[1] == 0);
    assert(observation.result[2] == ESP_ERR_TIMEOUT && observation.result[3] == ESP_OK);
    assert(observation.calls[static_cast<size_t>(StandbyStep::PrepareDarkFrame)] == display_calls);
    assert(cody_host_standby_transition(owner, false) == ESP_OK);
    assert(cody_host_standby_profile(owner, true, false, true, true) == ESP_OK);
    assert(cody_host_standby_transition(owner, true) == ESP_OK);
    observation = read_owner(owner);
    assert(observation.result[7] == 0 && observation.result[8] == 0); // Real policy keeps listening audio.
    assert(cody_host_standby_transition(owner, false) == ESP_OK);
    assert(cody_host_standby_profile(owner, true, false, false, true) == ESP_OK);
    assert(cody_host_standby_transition(owner, true) == ESP_OK);
    assert(read_owner(owner).result[7] == 1); // Decision only; host never physically sleeps.
    fault(owner, StandbyStep::Wireless, false, ESP_ERR_TIMEOUT);
    assert(cody_host_standby_transition(owner, false) == ESP_ERR_TIMEOUT);
    observation = read_owner(owner);
    assert(observation.result[0] == 0 && observation.result[1] == 0);
    assert(observation.result[6] == ESP_ERR_TIMEOUT);
    assert(observation.applied[static_cast<size_t>(StandbyStep::Wireless)] == 1);
    assert(observation.consistent[static_cast<size_t>(StandbyStep::Wireless)] == 1);
    cody_host_standby_destroy(owner);

    assert(cody_host_standby_create(&owner, false) == ESP_OK);
    fault(owner, StandbyStep::Distance, true, ESP_ERR_TIMEOUT, 2);
    fault(owner, StandbyStep::Distance, false, ESP_FAIL);
    assert(cody_host_standby_transition(owner, true) == ESP_ERR_TIMEOUT);
    observation = read_owner(owner);
    assert(observation.result[1] == 1 && observation.result[2] == ESP_ERR_TIMEOUT);
    assert(observation.result[3] == ESP_FAIL && observation.result[4] == ESP_ERR_TIMEOUT);
    assert(cody_host_standby_transition(owner, false) == ESP_ERR_INVALID_STATE);
    observation = read_owner(owner);
    assert(observation.result[1] == 1 && observation.result[2] == ESP_ERR_TIMEOUT);
    cody_host_standby_destroy(owner);

    assert(cody_host_standby_create(&owner, false) == ESP_OK);
    assert(cody_host_standby_transition(owner, true) == ESP_OK);
    fault(owner, StandbyStep::Wireless, false, ESP_ERR_TIMEOUT);
    fault(owner, StandbyStep::Wireless, true, ESP_FAIL);
    assert(cody_host_standby_transition(owner, false) == ESP_FAIL);
    observation = read_owner(owner);
    assert(observation.result[0] == 1 && observation.result[1] == 0);
    assert(observation.result[2] == ESP_FAIL && observation.result[6] == ESP_ERR_TIMEOUT);
    cody_host_standby_destroy(owner);
    return 0;
}
