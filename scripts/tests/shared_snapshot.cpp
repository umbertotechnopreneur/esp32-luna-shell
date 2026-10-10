#include "LUNASHELL_snapshot_aging.hpp"
#include "LUNASHELL_http_json.hpp"
#include "LUNASHELL_rtc_calendar.hpp"
#include <cassert>
#include <memory>
#include <string>

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
}

// No parameters: deterministic host inputs exercising production aging, RTC and serializer.
// Exception: Assertion failure identifies a lost timestamp, fabricated current value or swallowed sink error.
int main()
{
    using namespace LUNASHELL;
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
