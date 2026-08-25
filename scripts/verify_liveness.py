"""Static, hardware-free checks for the firmware liveness contract."""

from __future__ import annotations

from pathlib import Path

from firmware_profile import read_profile_defaults


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_contains(text: str, fragment: str, source: str) -> None:
    require(fragment in text, f"{fragment!r} missing from {source}")


def function_text(text: str, signature: str, source: str) -> str:
    signature_at = text.find(signature)
    require(signature_at >= 0, f"{signature!r} missing from {source}")
    body_at = text.find("{", signature_at + len(signature))
    require(body_at >= 0, f"body for {signature!r} missing from {source}")
    depth = 0
    for index in range(body_at, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[signature_at : index + 1]
    raise AssertionError(f"unterminated body for {signature!r} in {source}")


def require_order(text: str, first: str, second: str, source: str) -> None:
    first_at = text.find(first)
    second_at = text.find(second)
    require(first_at >= 0, f"{first!r} missing from {source}")
    require(second_at >= 0, f"{second!r} missing from {source}")
    require(first_at < second_at, f"{first!r} must precede {second!r} in {source}")


def main() -> int:
    defaults = read_profile_defaults()
    for setting in (
        "CONFIG_BOOTLOADER_WDT_ENABLE=y",
        "CONFIG_ESP_INT_WDT=y",
        "CONFIG_ESP_INT_WDT_CHECK_CPU1=y",
        "CONFIG_ESP_TASK_WDT_EN=y",
        "CONFIG_ESP_TASK_WDT_INIT=y",
        "CONFIG_ESP_TASK_WDT_PANIC=y",
        "CONFIG_ESP_TASK_WDT_TIMEOUT_S=12",
        "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y",
        "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y",
    ):
        require_contains(defaults, setting, "full profile defaults")

    liveness = read("components/LUNASHELL_platform/LUNASHELL_liveness.cpp")
    header = read(
        "components/LUNASHELL_platform/include/LUNASHELL_liveness.h"
    )
    for point in (
        "LUNASHELL_LIVENESS_GUI",
        "LUNASHELL_LIVENESS_DISPLAY",
        "LUNASHELL_LIVENESS_SETTINGS",
        "LUNASHELL_LIVENESS_PROVISIONING",
        "LUNASHELL_LIVENESS_BOOT_AUDIO",
        "LUNASHELL_LIVENESS_SNAPSHOT",
        "LUNASHELL_LIVENESS_RADIOS",
        "LUNASHELL_LIVENESS_WIFI",
        "LUNASHELL_LIVENESS_WIFI_SCAN",
        "LUNASHELL_LIVENESS_WIFI_EVENTS",
        "LUNASHELL_LIVENESS_STARTUP",
    ):
        require_contains(header, point, "LUNASHELL_liveness.h")
    for fragment in (
        "esp_task_wdt_reconfigure(&config)",
        ".trigger_panic = true",
        "esp_task_wdt_add_user",
        "esp_task_wdt_reset_user",
        "esp_task_wdt_delete_user",
        "esp_task_wdt_isr_user_handler",
        "RTC_NOINIT_ATTR",
        's_display_watch_name[] = "lvgl-worker"',
    ):
        require_contains(liveness, fragment, "LUNASHELL_liveness.cpp")
    require(
        "LUNASHELL_liveness_set_standby" not in header + liveness,
        "obsolete global liveness standby API returned",
    )

    main_source = read("main/main.cpp")
    app_main = function_text(main_source, 'extern "C" void app_main(void)', "main.cpp")
    require_order(
        app_main,
        "LUNASHELL_liveness_init()",
        "StartupWatch startup_watch",
        "main.cpp app_main",
    )
    require_order(
        app_main,
        "xSemaphoreTake(",
        "startup_watch.complete()",
        "main.cpp app_main",
    )
    brookesia_task = function_text(main_source, "void brookesia_task()", "main.cpp")
    require_order(
        brookesia_task,
        "LUNASHELL_display_apply_saved_auto_rotation()",
        "s_brookesia_ready.store(true",
        "main.cpp brookesia_task",
    )
    require_contains(
        main_source,
        "lv_timer_create(\n        display_liveness_timer_callback",
        "main.cpp",
    )

    app = read("components/LUNASHELL_app/LUNASHELL_app.cpp")
    on_stop = function_text(app, "App::on_stop(", "LUNASHELL_app.cpp")
    require_order(
        on_stop,
        "LUNASHELL_liveness_watch_begin(",
        "LUNASHELL_liveness_watch_end(",
        "App::on_stop",
    )
    on_timer = function_text(app, "App::on_timer(", "LUNASHELL_app.cpp")
    require_contains(on_timer, "GuiTimerProgress timer_progress", "App::on_timer")
    on_pause = function_text(app, "App::on_pause(", "LUNASHELL_app.cpp")
    require_order(
        on_pause,
        "if (!gui_liveness_watch_active_)",
        "standby_ui_state_ = StandbyUiState::Quiescing",
        "App::on_pause",
    )
    require_contains(
        app,
        "GUI timer dispatched without an active watchdog",
        "GuiTimerProgress",
    )

    for source_path, task_signature, completion_signal in (
        (
            "components/LUNASHELL_boot/LUNASHELL_boot_audio.c",
            "static void complete_audio_task(",
            "signal_audio_complete();",
        ),
        (
            "components/LUNASHELL_platform/LUNASHELL_settings.cpp",
            "void settings_worker(",
            "xSemaphoreGive(s_completion)",
        ),
        (
            "components/LUNASHELL_platform/LUNASHELL_provisioning.cpp",
            "void manager_operation_task(",
            "xSemaphoreGive(s_manager_stop_done)",
        ),
    ):
        source = read(source_path)
        task = function_text(source, task_signature, source_path)
        require_order(
            task,
            "LUNASHELL_liveness_watch_end(",
            completion_signal,
            source_path,
        )
    boot_audio = read("components/LUNASHELL_boot/LUNASHELL_boot_audio.c")
    require_contains(
        boot_audio,
        "timeout_ms_64 + BOOT_AUDIO_WDT_MARGIN_MS > watchdog_timeout_ms",
        "LUNASHELL_boot_audio.c",
    )

    snapshot = read(
        "components/LUNASHELL_platform/LUNASHELL_snapshot_scheduler.cpp"
    )
    snapshot_task = function_text(
        snapshot,
        "void full_snapshot_task(",
        "LUNASHELL_snapshot_scheduler.cpp",
    )
    require(
        snapshot_task.count("xEventGroupSetBits(") == 1,
        "snapshot quiescence ack must be published only from the inactive branch",
    )
    require_order(
        snapshot_task,
        "LUNASHELL_liveness_watch_end(",
        "xEventGroupSetBits(",
        "full_snapshot_task inactive branch",
    )

    wifi = read("components/LUNASHELL_platform/LUNASHELL_wifi.cpp")
    require("portMAX_DELAY" not in wifi, "Wi-Fi regained an unbounded wait")
    for fragment in (
        "wifi_lifecycle_lock(",
        "wifi_lifecycle_try_lock(",
        'wifi_lifecycle_try_lock("reconnect callback")',
    ):
        require_contains(wifi, fragment, "LUNASHELL_wifi.cpp")
    reconnect_callback = function_text(
        wifi,
        "void reconnect_timer_callback(",
        "LUNASHELL_wifi.cpp",
    )
    schedule_reconnect = function_text(
        wifi,
        "esp_err_t schedule_reconnect(",
        "LUNASHELL_wifi.cpp",
    )
    cancel_reconnect = function_text(
        wifi,
        "esp_err_t cancel_reconnect(",
        "LUNASHELL_wifi.cpp",
    )
    schedule_timer_at = schedule_reconnect.find("xTimerChangePeriod(")
    schedule_lock_at = schedule_reconnect.rfind(
        "wifi_lock();",
        0,
        schedule_timer_at,
    )
    schedule_commit_at = schedule_reconnect.find(
        "s_status.reconnect_pending = true",
        schedule_timer_at,
    )
    schedule_unlock_at = schedule_reconnect.find(
        "wifi_unlock();",
        schedule_commit_at,
    )
    schedule_publish_at = schedule_reconnect.find(
        "publish_snapshot();",
        schedule_unlock_at,
    )
    require(
        0 <= schedule_lock_at < schedule_timer_at < schedule_commit_at
        < schedule_unlock_at < schedule_publish_at,
        "reconnect schedule command and published pending state must commit "
        "under one Wi-Fi status transaction",
    )
    schedule_intent_guard_at = schedule_reconnect.find(
        "if (!s_initialized || !s_status.started || !s_connect_requested)"
    )
    require(
        0 <= schedule_lock_at < schedule_intent_guard_at < schedule_timer_at,
        "reconnect scheduling must revalidate connection intent under the "
        "timer-command lock",
    )
    cancel_timer_at = cancel_reconnect.find("xTimerStop(")
    cancel_lock_at = cancel_reconnect.rfind(
        "wifi_lock();",
        0,
        cancel_timer_at,
    )
    cancel_commit_at = cancel_reconnect.find(
        "s_status.reconnect_pending = false",
        cancel_timer_at,
    )
    cancel_unlock_at = cancel_reconnect.find(
        "wifi_unlock();",
        cancel_commit_at,
    )
    cancel_publish_at = cancel_reconnect.find(
        "publish_snapshot();",
        cancel_unlock_at,
    )
    require(
        0 <= cancel_lock_at < cancel_timer_at < cancel_commit_at
        < cancel_unlock_at < cancel_publish_at,
        "reconnect cancel command and published cleared state must commit "
        "under one Wi-Fi status transaction",
    )
    cancel_intent_clear_at = cancel_reconnect.find(
        "s_connect_requested = false;",
        cancel_lock_at,
    )
    cancel_intent_restore_at = cancel_reconnect.find(
        "s_connect_requested = previous_connect_requested;",
        cancel_timer_at,
    )
    require(
        0 <= cancel_intent_clear_at < cancel_timer_at < cancel_intent_restore_at,
        "explicit reconnect cancellation must clear intent before the timer "
        "command and restore it only when an aborting transaction fails",
    )
    require(
        "record_error(" not in schedule_reconnect
        and "record_error(" not in cancel_reconnect,
        "reconnect timer transactions must publish errors only after releasing "
        "wifi_lock",
    )
    require_order(
        reconnect_callback,
        'wifi_lifecycle_try_lock("reconnect callback")',
        "LUNASHELL_liveness_watch_begin(",
        "reconnect_timer_callback",
    )
    require(
        "wifi_lifecycle_lock(" not in reconnect_callback,
        "reconnect callback must never block the timer daemon on lifecycle",
    )
    callback_busy_end = reconnect_callback.find("WifiLifecycleGuard lifecycle_guard")
    callback_busy = reconnect_callback[:callback_busy_end]
    callback_pending_clear_at = callback_busy.find(
        "s_status.reconnect_pending = false;"
    )
    callback_reschedule_at = callback_busy.find("schedule_reconnect(")
    require(
        0 <= callback_pending_clear_at < callback_reschedule_at,
        "a fired reconnect timer must clear pending state before deferred "
        "rescheduling",
    )
    wifi_event_handler = function_text(
        wifi,
        "void wifi_event_handler(",
        "LUNASHELL_wifi.cpp",
    )
    require_contains(
        wifi_event_handler,
        "WifiEventWatch event_watch",
        "wifi_event_handler",
    )
    scan_start = function_text(
        wifi,
        "static esp_err_t wifi_start_scan_impl(",
        "LUNASHELL_wifi.cpp",
    )
    require_order(
        scan_start,
        "LUNASHELL_LIVENESS_WIFI_SCAN",
        "esp_wifi_scan_start(&config, false)",
        "wifi_start_scan_impl",
    )
    scan_done = function_text(wifi, "void handle_scan_done(", "LUNASHELL_wifi.cpp")
    require_contains(
        scan_done,
        "finish_scan_watch_if_active(error)",
        "handle_scan_done",
    )
    wifi_stop = function_text(
        wifi,
        "static esp_err_t wifi_stop_impl(",
        "LUNASHELL_wifi.cpp",
    )
    require_contains(
        wifi_stop,
        "finish_scan_watch_if_active(ESP_OK)",
        "wifi_stop_impl",
    )
    require_contains(
        wifi_stop,
        "cancel_reconnect(true, true)",
        "atomic Wi-Fi stop intent cancellation",
    )
    wifi_disconnect = function_text(
        wifi,
        "static esp_err_t wifi_disconnect_impl(",
        "LUNASHELL_wifi.cpp",
    )
    require_contains(
        wifi_disconnect,
        "cancel_reconnect(true, false)",
        "atomic explicit Wi-Fi disconnect intent cancellation",
    )
    for wrapper, expected in (
        ("LUNASHELL_wifi_init(", 'run_watched_wifi_operation("initialization"'),
        ("LUNASHELL_wifi_stop(", 'run_watched_wifi_operation("stop"'),
        ("LUNASHELL_wifi_connect(", "LUNASHELL_liveness_watch_begin("),
        ("LUNASHELL_wifi_disconnect(", 'run_watched_wifi_operation("disconnect"'),
        ("LUNASHELL_wifi_start_scan(", 'run_watched_wifi_operation("scan"'),
    ):
        wrapper_text = function_text(wifi, wrapper, "LUNASHELL_wifi.cpp")
        require_contains(wrapper_text, expected, wrapper)

    health = read("components/LUNASHELL_platform/LUNASHELL_http_json.cpp")
    for fragment in (
        r'\"active_mask\"',
        r'\"last_progress\"',
        r'\"previous_timeout\"',
    ):
        require_contains(health, fragment, "LUNASHELL_http_json.cpp")

    print("LUNASHELL liveness contract verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
