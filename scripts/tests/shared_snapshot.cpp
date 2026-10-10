/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include "LUNASHELL_snapshot_aging.hpp"
#include "LUNASHELL_http_json.hpp"
#include "LUNASHELL_rtc_calendar.hpp"
#include "CODY_host_snapshot.hpp"
#include <cassert>
#include <memory>
#include <string>
#include <algorithm>
#include <array>
#include <iterator>

namespace {
struct Sink {
    std::string text;
    esp_err_t error = ESP_OK;
    bool finished = false;
};
// context: Host test sink with explicit injected transport error.
// data: Borrowed production serializer chunk.
// size: Exact original byte count.
// final: Explicit completion marker; failed writes cannot complete the response.
// Exception: Returns the injected real transport error.
esp_err_t write(void *context, const char *data, std::size_t size, bool final)
{
    auto &sink = *static_cast<Sink *>(context);
    if (sink.error != ESP_OK) { return sink.error; }
    if (final) { sink.finished = true; }
    else { sink.text.append(data, size); }
    return ESP_OK;
}

// No parameters: actual native batch boundary with valid, duplicate, unknown and overflowing records.
// Exception: Assertion failure identifies partial owner mutation or partially exposed metadata.
void verify_batch_snapshot_transport()
{
    using namespace CODY::host_snapshot;
    std::unique_ptr<void, decltype(&cody_host_snapshot_destroy)> owner(
        cody_host_snapshot_create(), &cody_host_snapshot_destroy);
    assert(owner);
    const FieldInput initial[] = {
        {"battery.last_success_at_us", "1000000"}, {"battery.last_attempt_at_us", "1000000"},
        {"battery.health", "ready"}, {"battery.last_error", "0"},
        {"battery.data_valid", "true"}, {"battery.data_stale", "false"},
        {"battery.battery_present", "true"},
        {"battery.level_percent_valid", "true"}, {"battery.level_percent", "47"},
        {"sequence", "11"}, {"last_update_attempt_at_us", "1000000"}
    };
    std::size_t failed = 0;
    assert(cody_host_snapshot_import(owner.get(), initial, std::size(initial), 1000000, &failed) == ESP_OK);
    assert(failed == std::size(initial));
    const char *paths[] = {"battery"};
    std::array<int64_t, kComponentMetadataFields> metadata{};
    assert(cody_host_snapshot_components(owner.get(), paths, 1, metadata.data(), metadata.size()) == ESP_OK);
    assert(metadata[2] == 1 && metadata[3] == 0 && metadata[4] == 1000000 && metadata[5] == 1000000);
    const char *json = nullptr;
    std::size_t size = 0;
    assert(cody_host_snapshot_json(owner.get(), "/api/v1/info", 1000000, &json, &size) == ESP_OK);
    const std::string original(json, size);
    assert(original.find("\"level_percent\":47") != std::string::npos);
    const FieldInput overflowing[] = {
        {"battery.level_percent", "95"}, {"battery.last_success_at_us", "999999999999999999999999"}
    };
    assert(cody_host_snapshot_import(owner.get(), overflowing, 2, 1000000, &failed) == ESP_ERR_INVALID_SIZE);
    assert(failed == 1);
    const FieldInput duplicated[] = {{"battery.level_percent", "50"}, {"battery.level_percent", "51"}};
    assert(cody_host_snapshot_import(owner.get(), duplicated, 2, 1000000, &failed) == ESP_ERR_INVALID_ARG);
    assert(failed == 1);
    const FieldInput unknown[] = {{"battery.level_percent", "52"}, {"battery.unknown_field", "0"}};
    assert(cody_host_snapshot_import(owner.get(), unknown, 2, 1000000, &failed) == ESP_ERR_NOT_FOUND);
    assert(failed == 1);
    assert(cody_host_snapshot_import(owner.get(), initial, 0, 1000000, &failed) == ESP_ERR_INVALID_SIZE);
    assert(cody_host_snapshot_import(owner.get(), initial, kMaximumInputFields + 1, 1000000, &failed) == ESP_ERR_INVALID_SIZE);
    assert(cody_host_snapshot_json(owner.get(), "/api/v1/info", 1000000, &json, &size) == ESP_OK);
    assert(std::string(json, size) == original);
    std::array<int64_t, 2 * kComponentMetadataFields> unpublished;
    unpublished.fill(-777);
    const char *invalid_paths[] = {"battery", "unknown_component"};
    assert(cody_host_snapshot_components(owner.get(), invalid_paths, 2, unpublished.data(), unpublished.size()) == ESP_ERR_NOT_FOUND);
    // value: One output element that must remain untouched after a failed batch projection.
    assert(std::all_of(unpublished.begin(), unpublished.end(), [](int64_t value) { return value == -777; }));
    assert(cody_host_snapshot_components(owner.get(), nullptr, 0, nullptr, 0) == ESP_OK);
}
}

// No parameters: deterministic host inputs exercising production aging, RTC and serializer.
// Exception: Assertion failure identifies a lost timestamp, fabricated current value or swallowed sink error.
int main()
{
    using namespace LUNASHELL;
    verify_batch_snapshot_transport();
    // The host owns this full schema on its heap, not an embedded task stack.
    auto snapshot = std::make_unique<DeviceSnapshot>();
    snapshot->battery.health = DeviceHealth::Ready;
    snapshot->battery.last_error = ESP_OK;
    snapshot->battery.data_valid = true;
    snapshot->battery.last_success_at_us = 1000000;
    snapshot->battery.last_attempt_at_us = 1000000;
    snapshot->battery.level_percent_valid = true;
    snapshot->battery.level_percent = 47;
    assert(measured_sample_ttl_us(*snapshot) == 5000000);
    // Bounded canonical projections use these exact public aging helpers too.
    ComponentSnapshot bounded = snapshot->battery;
    age_measured_sample(bounded, 6000000, measured_sample_ttl_us(*snapshot));
    assert(!bounded.data_valid && bounded.data_stale);
    apply_measured_sample_aging(*snapshot, 5999999);
    assert(snapshot->battery.data_valid);
    apply_measured_sample_aging(*snapshot, 6000000);
    assert(!snapshot->battery.data_valid && snapshot->battery.data_stale);
    assert(snapshot->battery.level_percent == 47 && snapshot->battery.last_success_at_us == 1000000);
    Sink body;
    HttpJsonWriter json({&body, write}, 6000000);
    httpJsonWriteInfo(json, *snapshot);
    assert(json.finish() == ESP_OK && body.finished);
    assert(body.text.find("\"level_percent\":null") != std::string::npos);
    assert(body.text.find("\"last_success_at_us\":1000000") != std::string::npos);
    assert(body.text.find("\"ssid\"") == std::string::npos);
    Sink failed;
    failed.error = ESP_ERR_TIMEOUT;
    HttpJsonWriter broken({&failed, write}, 6000000);
    httpJsonWriteDiagnostics(broken, *snapshot);
    assert(broken.finish() == ESP_ERR_TIMEOUT && !failed.finished);
    uint8_t raw[7]{0x59, 0x58, 0x23, 0x29, 0, 0x02, 0x24};
    LUNASHELL_calendar_time_t calendar{};
    assert(rtc_calendar::decode_registers(raw, calendar) == ESP_OK);
    assert(calendar.year == 2024 && calendar.month == 2 && calendar.day == 29);
    raw[1] = 0x6a;
    assert(rtc_calendar::decode_registers(raw, calendar) == ESP_ERR_INVALID_RESPONSE);
    raw[1] = 0x58;
    raw[0] |= 0x80;
    assert(rtc_calendar::decode_registers(raw, calendar) == ESP_ERR_INVALID_STATE);
    return 0;
}
