"""Verify the non-UI HTTP API settings and lifecycle contract."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "components/LUNASHELL_platform/include/LUNASHELL_http_server.hpp"
JSON_HEADER = ROOT / "components/LUNASHELL_platform/include/LUNASHELL_http_json.hpp"
SOURCE = ROOT / "components/LUNASHELL_platform/LUNASHELL_http_server.cpp"
JSON_SOURCE = ROOT / "components/LUNASHELL_platform/LUNASHELL_http_json.cpp"
DOC = ROOT / "docs/architecture/http-device-api.md"

MEMORY_FIELDS = (
    "internal_heap_total_bytes",
    "internal_minimum_free_bytes",
    "internal_allocated_blocks",
    "internal_free_blocks",
    "internal_total_blocks",
    "psram_minimum_free_bytes",
    "psram_allocated_blocks",
    "psram_free_blocks",
    "psram_total_blocks",
    "allocation_failure_monitor_active",
    "allocation_failure_count",
    "largest_failed_allocation_bytes",
    "failed_allocation_caps_seen_mask",
)


def require_contains(text: str, fragment: str, label: str) -> None:
    if fragment not in text:
        raise AssertionError(f"{label} is missing: {fragment}")


def function_text(text: str, signature: str, label: str) -> str:
    signature_at = text.find(signature)
    if signature_at < 0:
        raise AssertionError(f"{label} is missing: {signature}")
    body_at = text.find("{", signature_at + len(signature))
    if body_at < 0:
        raise AssertionError(f"{label} has no body: {signature}")
    depth = 0
    for index in range(body_at, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[signature_at : index + 1]
    raise AssertionError(f"{label} has an unterminated body: {signature}")


def main() -> int:
    header = HEADER.read_text(encoding="utf-8")
    json_header = JSON_HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    json_source = JSON_SOURCE.read_text(encoding="utf-8")
    doc = DOC.read_text(encoding="utf-8")

    require_contains(header, "esp_err_t httpServerSetApiKey(const char *api_key);", "header API")
    require_contains(source, "esp_err_t validateApiKeyValue(const char *value)", "bounded validation")
    require_contains(source, "error = settings.setString(kApiKeySetting, api_key);", "persistent API key write")
    require_contains(source, "error = loadApiKeyValue(api_key);", "active credential update")
    require_contains(source, "HTTP API key updated without changing server lifecycle", "lifecycle guard")
    require_contains(source, "error = settings.getBool(kEnabledSetting, &enabled);", "persisted enable read")
    require_contains(source, "error = loadApiKeyFromSettings();", "persisted key read")
    require_contains(
        source,
        "event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP;",
        "station lifecycle start",
    )
    require_contains(source, "s_server_state = ServerLifecycleState::CleanupRequired;", "retained failed-stop ownership")
    require_contains(source, "handle retained for reconciliation", "failed-stop visibility")
    require_contains(source, "recordEventLifecycleFailure();", "network callback failure metric")
    require_contains(source, "esp_err_t reconcileServerLocked(bool should_run)", "single lifecycle reconciler")
    require_contains(source, "s_initialized.load(std::memory_order_acquire)", "serialized initialization state")
    require_contains(json_source, '\\"stop_failure_count\\"', "HTTP stop-failure metric")
    require_contains(json_source, '\\"event_lifecycle_failure_count\\"', "HTTP event-failure metric")
    require_contains(json_source, '\\"last_lifecycle_error\\"', "HTTP lifecycle error metric")
    for field in (
        "server_cleanup_required",
        "server_handle_retained",
        "server_accepting_requests",
    ):
        require_contains(
            json_source,
            f'\\"{field}\\"',
            "HTTP lifecycle JSON",
        )
        require_contains(doc, field, "HTTP lifecycle documentation")
    require_contains(doc, "httpServerSetApiKey()", "API documentation")

    start_server = function_text(
        source,
        "esp_err_t startServerLocked()",
        "HTTP start lifecycle",
    )
    if "(void)httpd_stop" in start_server:
        raise AssertionError("HTTP start lifecycle must not discard cleanup errors")
    failed_start_cleanup_at = start_server.find(
        "const esp_err_t cleanup_error = httpd_stop(server);"
    )
    cleanup_failure_at = start_server.find(
        "if (cleanup_error != ESP_OK)",
        failed_start_cleanup_at,
    )
    retained_handle_at = start_server.find(
        "s_server = server;",
        cleanup_failure_at,
    )
    primary_return_at = start_server.find("return error;", retained_handle_at)
    if not (
        0 <= failed_start_cleanup_at < cleanup_failure_at <
        retained_handle_at < primary_return_at
    ):
        raise AssertionError(
            "failed HTTP route startup must retain its handle on cleanup "
            "failure and return the primary error"
        )
    failed_start_state_at = start_server.find(
        "s_server_state = ServerLifecycleState::CleanupRequired;",
        retained_handle_at,
    )
    failed_start_serving_at = start_server.find(
        "publishLifecycleLocked(true, false);",
        failed_start_state_at,
    )
    if not (
        retained_handle_at < failed_start_state_at < failed_start_serving_at <
        primary_return_at
    ):
        raise AssertionError(
            "a failed-start cleanup must expose retained serving possibility "
            "without accepting data requests"
        )

    stop_server = function_text(
        source,
        "esp_err_t stopServerLocked()",
        "HTTP stop lifecycle",
    )
    stop_call_at = stop_server.find("const esp_err_t error = httpd_stop(s_server);")
    stop_failure_at = stop_server.find("if (error != ESP_OK)", stop_call_at)
    retained_state_at = stop_server.find(
        "s_server_state = ServerLifecycleState::CleanupRequired;",
        stop_failure_at,
    )
    stop_failure_return_at = stop_server.find("return error;", retained_state_at)
    handle_clear_at = stop_server.find("s_server = nullptr;", stop_failure_return_at)
    stop_success_at = stop_server.find("recordServerStop();", handle_clear_at)
    if not (
        0 <= stop_call_at < stop_failure_at < retained_state_at <
        stop_failure_return_at < handle_clear_at < stop_success_at
    ):
        raise AssertionError(
            "HTTP stop must retain ownership on failure and record success "
            "only after cleanup succeeds"
        )
    stop_fail_closed_at = stop_server.find(
        "publishLifecycleLocked(true, false);",
        0,
        stop_call_at,
    )
    stop_failure_serving_at = stop_server.find(
        "publishLifecycleLocked(true, false);",
        retained_state_at,
    )
    stopped_publish_at = stop_server.find(
        "publishLifecycleLocked(false, false);",
        handle_clear_at,
    )
    if not (
        0 <= stop_fail_closed_at < stop_call_at < retained_state_at <
        stop_failure_serving_at < stop_failure_return_at < handle_clear_at <
        stopped_publish_at
    ):
        raise AssertionError(
            "HTTP stop must fail-close before ESP-IDF teardown, retain "
            "serving-possible state on failure, and publish stopped only "
            "after handle release"
        )

    lifecycle_snapshot = function_text(
        source,
        "LUNASHELL::HttpLifecycleView snapshotLifecycle()",
        "atomic HTTP lifecycle snapshot",
    )
    require_contains(
        lifecycle_snapshot,
        "s_lifecycle_flags.load(std::memory_order_acquire)",
        "coherent HTTP lifecycle read",
    )
    request_handler = function_text(
        source,
        "esp_err_t handleApiRequest(httpd_req_t *request)",
        "HTTP request lifecycle gate",
    )
    lifecycle_read_at = request_handler.find(
        "LUNASHELL::HttpLifecycleView lifecycle = snapshotLifecycle();"
    )
    desired_read_at = request_handler.find(
        "s_enabled.load(std::memory_order_acquire);",
        0,
        lifecycle_read_at,
    )
    effective_gate_at = request_handler.find(
        "lifecycle.accepting_requests =\n"
        "        desired_enabled &&\n"
        "        lifecycle.running &&\n"
        "        !lifecycle.cleanup_required &&\n"
        "        lifecycle.handle_retained &&\n"
        "        lifecycle.accepting_requests;",
        lifecycle_read_at,
    )
    fail_closed_at = request_handler.find(
        "if (!lifecycle.accepting_requests && !is_health)",
        effective_gate_at,
    )
    auth_at = request_handler.find("if (!is_health)", fail_closed_at)
    unhealthy_status_at = request_handler.find(
        'is_health && !lifecycle.accepting_requests\n'
        '            ? "503 Service Unavailable"',
        auth_at,
    )
    if not (
        0 <= desired_read_at < lifecycle_read_at < effective_gate_at <
        fail_closed_at < auth_at < unhealthy_status_at
    ):
        raise AssertionError(
            "HTTP protected routes must fail closed on desired-disabled or "
            "non-Running lifecycle before authentication, and the public "
            "health route must expose a 503 lifecycle report"
        )
    require_contains(
        request_handler,
        "const esp_err_t response_setup_error = prepareJsonResponse(",
        "checked HTTP response setup",
    )
    if "(void)httpd_resp_" in source or "(void)httpd_resp_" in json_source:
        raise AssertionError("HTTP response metadata errors are discarded")
    require_contains(
        json_header,
        "esp_err_t finish();",
        "HTTP writer transport error contract",
    )
    require_contains(
        request_handler,
        "const esp_err_t response_error = response.finish();",
        "HTTP response transport propagation",
    )
    require_contains(
        request_handler,
        "return response_error;",
        "HTTP response transport propagation",
    )

    reconcile_server = function_text(
        source,
        "esp_err_t reconcileServer(bool should_run)",
        "locked HTTP lifecycle reconciliation",
    )
    require_contains(
        reconcile_server,
        "std::lock_guard<std::mutex> lifecycle_lock(s_lifecycle_mutex);",
        "HTTP lifecycle reconciler lock",
    )
    require_contains(
        reconcile_server,
        "return reconcileServerLocked(should_run);",
        "HTTP lifecycle reconciler dispatch",
    )

    observed_network = function_text(
        source,
        "esp_err_t reconcileObservedNetworkState(",
        "observed HTTP network reconciliation",
    )
    for fragment in (
        "if (!s_network_event_seen)",
        "s_network_has_ip = observed_has_ip;",
        "s_infrastructure_ready = true;",
        "s_initialized.store(true, std::memory_order_release);",
        "return reconcileServerLocked(",
        "s_enabled.load(std::memory_order_acquire) && s_network_has_ip",
    ):
        require_contains(
            observed_network,
            fragment,
            "network observation commit/reconciliation",
        )
    infrastructure_commit_at = observed_network.find(
        "s_infrastructure_ready = true;"
    )
    initialized_commit_at = observed_network.find(
        "s_initialized.store(true, std::memory_order_release);"
    )
    observed_reconcile_at = observed_network.find("return reconcileServerLocked(")
    if not (
        0 <= infrastructure_commit_at < initialized_commit_at <
        observed_reconcile_at
    ):
        raise AssertionError(
            "HTTP infrastructure and initialization must commit atomically "
            "before lifecycle reconciliation"
        )

    event_handler = function_text(
        source,
        "void networkEventHandler(",
        "HTTP network event lifecycle",
    )
    for discarded_call in (
        "startServerLocked();",
        "stopServerLocked();",
        "(void)reconcileServerLocked(",
    ):
        if discarded_call in event_handler:
            raise AssertionError(
                f"HTTP network callback discards a lifecycle error: {discarded_call}"
            )
    event_seen_at = event_handler.find("s_network_event_seen = true;")
    event_ip_at = event_handler.find("s_network_has_ip = has_ip;", event_seen_at)
    infrastructure_gate_at = event_handler.find(
        "deferred =\n            !s_infrastructure_ready ||\n"
        "            !s_initialized.load(std::memory_order_acquire);",
        event_ip_at,
    )
    event_reconcile_at = event_handler.find(
        "error = reconcileServerLocked(",
        infrastructure_gate_at,
    )
    if not (
        0 <= event_seen_at < event_ip_at < infrastructure_gate_at <
        event_reconcile_at
    ):
        raise AssertionError(
            "HTTP events must record their latest IP state, defer before "
            "infrastructure commit, then reconcile"
        )

    init_server = function_text(
        source,
        "esp_err_t httpServerInitLocked()",
        "locked HTTP initialization",
    )
    public_init = function_text(
        source,
        "esp_err_t httpServerInit()",
        "public HTTP initialization",
    )
    require_contains(
        public_init,
        "std::lock_guard<std::mutex> initialization_lock(s_initialization_mutex);",
        "serialized HTTP initialization",
    )
    require_contains(
        public_init,
        "return httpServerInitLocked();",
        "locked HTTP initialization dispatch",
    )
    init_reconcile_at = init_server.find(
        "error = reconcileObservedNetworkState(wifi.ip_acquired, true);"
    )
    wifi_owner_init_at = init_server.find(
        "const esp_err_t wifi_error = LUNASHELL_wifi_init();"
    )
    ip_handler_register_at = init_server.find(
        "error = esp_event_handler_instance_register(\n"
        "            IP_EVENT,"
    )
    network_status_at = init_server.find(
        "LUNASHELL_wifi_get_status(&wifi);",
        ip_handler_register_at,
    )
    if not (
        0 <= wifi_owner_init_at < ip_handler_register_at < network_status_at <
        init_reconcile_at
    ):
        raise AssertionError(
            "HTTP init must establish the Wi-Fi event owner before its own "
            "handlers, then observe status before the atomic commit"
        )
    init_reconcile_failure_at = init_server.find(
        "if (error != ESP_OK)",
        init_reconcile_at,
    )
    init_reconcile_return_at = init_server.find(
        "return error;",
        init_reconcile_failure_at,
    )
    if not (
        0 <= init_reconcile_at < init_reconcile_failure_at <
        init_reconcile_return_at
    ):
        raise AssertionError(
            "HTTP initialization must propagate immediate lifecycle "
            "reconciliation failures"
        )

    set_enabled = function_text(
        source,
        "esp_err_t httpServerSetEnabled(bool enabled)",
        "HTTP enabled preference transition",
    )
    set_lock_at = set_enabled.find(
        "std::lock_guard<std::mutex> initialization_lock(s_initialization_mutex);"
    )
    persisted_write_at = set_enabled.find(
        "error = settings.setBool(kEnabledSetting, enabled);"
    )
    enabled_publish_at = set_enabled.find("s_enabled.store(enabled);")
    locked_init_at = set_enabled.find("return httpServerInitLocked();")
    if not (
        0 <= set_lock_at < persisted_write_at < enabled_publish_at < locked_init_at
    ):
        raise AssertionError(
            "HTTP enabled preference write, publication, and incomplete-init "
            "reconciliation must share the initialization mutex"
        )
    if "return httpServerInit();" in set_enabled:
        raise AssertionError(
            "HTTP enabled transition must not reacquire its initialization mutex"
        )
    if source.count("httpServerInitLocked()") != 3:
        raise AssertionError(
            "locked HTTP initialization must be called only by its two "
            "initialization-mutex owners"
        )
    require_contains(
        set_enabled,
        "return reconcileServer(false);",
        "disabled HTTP lifecycle reconciliation",
    )
    require_contains(
        set_enabled,
        "error = reconcileObservedNetworkState(wifi.ip_acquired, false);",
        "enabled HTTP network reconciliation",
    )
    if "wifi.ip_acquired ?" in init_server or "if (wifi.ip_acquired)" in set_enabled:
        raise AssertionError(
            "HTTP disabled/no-IP paths must reconcile retained lifecycle state"
        )

    set_api_key = function_text(
        source,
        "esp_err_t httpServerSetApiKey(const char *api_key)",
        "HTTP API-key transition",
    )
    api_key_lock_at = set_api_key.find(
        "std::lock_guard<std::mutex> initialization_lock(s_initialization_mutex);"
    )
    api_key_persist_at = set_api_key.find(
        "error = settings.setString(kApiKeySetting, api_key);"
    )
    api_key_activate_at = set_api_key.find("error = loadApiKeyValue(api_key);")
    if not (0 <= api_key_lock_at < api_key_persist_at < api_key_activate_at):
        raise AssertionError(
            "HTTP API-key persistence and activation must serialize with "
            "initialization"
        )

    component_current = function_text(
        json_source,
        "bool componentCurrent(",
        "component current predicate",
    )
    for fragment in (
        "component.health == LUNASHELL::DeviceHealth::Ready",
        "component.last_error == ESP_OK",
        "component.data_valid",
        "!component.data_stale",
    ):
        require_contains(
            component_current,
            fragment,
            "component current predicate",
        )

    component_json = function_text(
        json_source,
        "void writeComponentStart(",
        "component freshness JSON serializer",
    )
    for fragment in (
        '\\"data_valid\\"',
        "json.boolean(component.data_valid)",
        '\\"data_stale\\"',
        "json.boolean(component.data_stale)",
        '\\"last_attempt_at_us\\"',
        '\\"last_success_at_us\\"',
    ):
        require_contains(
            component_json,
            fragment,
            "component freshness JSON contract",
        )

    battery_json = function_text(
        json_source,
        "void writeBattery(",
        "battery JSON serializer",
    )
    for fragment in (
        "const bool battery_current = componentCurrent(battery);",
        "writeComponentStart(json, battery);",
    ):
        require_contains(
            battery_json,
            fragment,
            "battery current/freshness JSON contract",
        )
    for field, value in (
        ("level_percent", "battery.level_percent"),
        ("battery_mv", "battery.battery_mv"),
    ):
        valid = f"{field}_valid"
        require_contains(
            battery_json,
            f"const bool {valid} =\n        battery_current &&\n        battery.{valid}",
            f"{field} effective current-field guard",
        )
        flag_at = battery_json.find(f'\\"{valid}\\"')
        flag_value_at = battery_json.find(
            f"json.boolean({valid})",
            flag_at,
        )
        value_at = battery_json.find(f'\\"{field}\\"', flag_value_at)
        guard_at = battery_json.find(f"if ({valid})", value_at)
        numeric_at = battery_json.find(f'json.format("%d", {value})', guard_at)
        else_at = battery_json.find("} else {", numeric_at)
        null_at = battery_json.find('json.raw("null")', else_at)
        if not (
            0 <= flag_at < flag_value_at < value_at < guard_at <
            numeric_at < else_at < null_at
        ):
            raise AssertionError(
                f"{field} must expose {valid} and serialize invalid data as null"
            )

    for field in MEMORY_FIELDS:
        require_contains(
            json_source,
            f'\\"{field}\\"',
            f"memory JSON field {field}",
        )
        require_contains(
            doc,
            f"`{field}`",
            f"documented memory JSON field {field}",
        )

    require_contains(
        source,
        "LUNASHELL::httpJsonWriteInfo(response, snapshot);",
        "redacted info dispatch",
    )
    require_contains(
        source,
        "LUNASHELL::httpJsonWriteDiagnostics(response, snapshot);",
        "complete diagnostics dispatch",
    )

    envelope_json = function_text(
        json_source,
        "void writeEnvelopeStart(",
        "snapshot JSON envelope",
    )
    for fragment in (
        '\\"last_update_error\\"',
        "esp_err_to_name(snapshot.last_update_error)",
    ):
        require_contains(
            envelope_json,
            fragment,
            "snapshot last-update error envelope",
        )

    info_json = function_text(
        json_source,
        "void httpJsonWriteInfo(",
        "info JSON serializer",
    )
    require_contains(
        info_json,
        '"LUNASHELL.device.info.v1"',
        "info JSON schema",
    )
    require_contains(
        info_json,
        "writeSnapshotData(json, snapshot, false);",
        "redacted info snapshot",
    )

    diagnostics_json = function_text(
        json_source,
        "void httpJsonWriteDiagnostics(",
        "diagnostics JSON serializer",
    )
    require_contains(
        diagnostics_json,
        '"LUNASHELL.device.diagnostics.v1"',
        "diagnostics JSON schema",
    )
    require_contains(
        diagnostics_json,
        "writeSnapshotData(json, snapshot, true);",
        "complete diagnostics snapshot",
    )

    snapshot_json = function_text(
        json_source,
        "void writeSnapshotData(",
        "DeviceSnapshot JSON serializer",
    )
    for group in (
        "battery",
        "imu",
        "sensor_fusion",
        "distance",
        "rtc",
        "gps",
        "audio",
        "radios",
        "storage",
        "diagnostics",
        "ui",
        "input",
        "power",
        "memory",
        "processor",
    ):
        require_contains(
            snapshot_json,
            f'\\"{group}\\"',
            f"complete DeviceSnapshot group {group}",
        )

    for signature, label in (
        ("void writeImu(", "IMU"),
        ("void writeSensorFusion(", "sensor fusion"),
        ("void writeRtcData(", "RTC"),
        ("void writeGpsData(", "GPS"),
        ("void writeWifi(", "Wi-Fi"),
        ("void writeBle(", "BLE"),
        ("void writeStorage(", "storage"),
        ("void writePowerManagement(", "power management"),
        ("void writeMemory(", "memory"),
        ("void writeProcessor(", "processor"),
        ("void writeTofSensor(", "ToF sensor"),
        ("void writeDistanceSettings(", "distance settings"),
        ("void writeDistanceCalibration(", "distance calibration"),
        ("void writeDistance(", "distance"),
    ):
        component_writer = function_text(
            json_source,
            signature,
            f"{label} JSON serializer",
        )
        require_contains(
            component_writer,
            "writeComponentStart(json,",
            f"{label} common component prefix",
        )

    gps_json = function_text(
        json_source,
        "void writeGpsData(",
        "GPS JSON serializer",
    )
    for fragment in (
        '\\"fix_valid\\"',
        "componentCurrent(gps)",
        "std::isfinite(gps.latitude_degrees)",
        "std::isfinite(gps.longitude_degrees)",
        'json.raw("null")',
    ):
        require_contains(gps_json, fragment, "guarded GPS numeric telemetry")

    storage_json = function_text(
        json_source,
        "void writeStorage(",
        "storage JSON serializer",
    )
    for field in (
        "logger_mirror",
        "mount_outcome",
        "mount_stage",
        "cleanup_stage",
        "cleanup_error",
        "initial_fatfs_result_valid",
        "initial_fatfs_result",
        "fatfs_result_valid",
        "fatfs_result",
        "cleanup_fatfs_result_valid",
        "cleanup_fatfs_result",
        "cleanup_complete",
        "poisoned",
        "mounted",
        "card_response_verified",
        "format_required",
        "authorization_consumed",
        "authorization_identity_matched",
        "format_attempted",
        "partition_written",
        "formatted",
        "mount_attempt_count",
        "capacity_bytes",
        "filesystem_total_bytes",
        "free_bytes",
        "logger_mirror_requested",
        "logger_mirror_enabled",
        "logger_mirror_errno",
        "logger_mirror_changed_at_us",
        "logger_mirror_failure_count",
    ):
        require_contains(
            storage_json,
            f'\\"{field}\\"',
            f"complete storage field {field}",
        )

    audio_json = function_text(
        json_source,
        "void writeAudio(",
        "audio JSON serializer",
    )
    for fragment in (
        '\\"channel_spectrum\\"',
        "audio.channel_spectrum[channel][band]",
        '\\"spectrum_sequence\\"',
    ):
        require_contains(audio_json, fragment, "complete audio spectrum")

    processor_json = function_text(
        json_source,
        "void writeProcessor(",
        "processor JSON serializer",
    )
    for fragment in (
        '\\"available\\"',
        '\\"utilization_available\\"',
        '\\"utilization_percent\\"',
        'json.raw("null")',
    ):
        require_contains(processor_json, fragment, "processor validity contract")

    for fragment in (
        "`LUNASHELL.device.diagnostics.v1`",
        "`last_update_error`",
        "`format_required`",
    ):
        require_contains(doc, fragment, "documented diagnostics contract")

    print(
        "HTTP API contract verification: PASS "
        "(serialized settings/lifecycle, deferred network reconciliation, "
        "nullable battery fields, raw heap telemetry)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
