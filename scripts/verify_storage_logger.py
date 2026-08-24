"""Static, hardware-free checks for removable-storage and logger scheduling."""

from __future__ import annotations

from pathlib import Path


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


def writer_wait_ms(lines_since_flush: int) -> int | None:
    """Return None for the intentionally unbounded empty-writer park."""
    return None if lines_since_flush == 0 else 100


def filesystem_space_refresh_due(
    *,
    forced: bool,
    tick_valid: bool,
    elapsed_ms: int,
    persisted_sequence: int,
    observed_sequence: int,
) -> bool:
    return (
        forced
        or not tick_valid
        or elapsed_ms >= 60_000
        or (
            persisted_sequence != observed_sequence
            and elapsed_ms >= 10_000
        )
    )


def enqueue_admitted(first_gate_read: bool, second_gate_read: bool) -> bool:
    """Model the callback's non-blocking double-check admission barrier."""
    return first_gate_read and second_gate_read


def generation_bound_failure_commits(
    expected_generation: int,
    current_generation: int,
) -> bool:
    return expected_generation == current_generation


def transition_rollback_result(primary_error: int, rollback_error: int) -> int:
    """A failed rollback is the more specific ownership result."""
    return rollback_error if rollback_error != 0 else primary_error


def bounded_batch_count(available_lines: int) -> int:
    return min(max(available_lines, 0), 8)


def drain_in_bounded_batches(available_lines: int) -> list[int]:
    batches: list[int] = []
    remaining = max(available_lines, 0)
    while remaining > 0:
        current = bounded_batch_count(remaining)
        batches.append(current)
        remaining -= current
    return batches


def decode_retained_point(
    version: int,
    raw_point: int,
    point_count: int,
) -> int | None:
    """Return point_count for no-point and None for an invalid trace."""
    if version != 2:
        return None
    if raw_point == 0xFFFF_FFFF:
        return point_count
    if 0 <= raw_point < point_count:
        return raw_point
    return None


def main() -> int:
    logger = read("components/LUNASHELL_logger/LUNASHELL_logger.c")
    logger_header = read(
        "components/LUNASHELL_logger/include/LUNASHELL_logger.h"
    )
    storage = read("components/LUNASHELL_platform/LUNASHELL_storage.cpp")
    platform_header = read(
        "components/LUNASHELL_platform/include/LUNASHELL_platform.h"
    )
    app = read("components/LUNASHELL_app/LUNASHELL_app.cpp")
    liveness = read("components/LUNASHELL_platform/LUNASHELL_liveness.cpp")
    liveness_header = read(
        "components/LUNASHELL_platform/include/LUNASHELL_liveness.h"
    )

    for point, name in (
        ("LUNASHELL_LIVENESS_STORAGE", '"storage-io"'),
        ("LUNASHELL_LIVENESS_STORAGE_FORMAT", '"storage-format"'),
        ("LUNASHELL_LIVENESS_LOGGER", '"logger-io"'),
    ):
        require_contains(liveness_header, point, "LUNASHELL_liveness.h")
        require_contains(liveness, name, "LUNASHELL_liveness.cpp")
    require(
        liveness_header.find("LUNASHELL_LIVENESS_STARTUP")
        < liveness_header.find("LUNASHELL_LIVENESS_STORAGE"),
        "new liveness domains changed retained numeric IDs of existing points",
    )

    for fragment in (
        "LUNASHELL_logger_io_watch_t",
        "LUNASHELL_logger_set_io_watch(",
        "persisted_write_sequence",
    ):
        require_contains(logger_header, fragment, "LUNASHELL_logger.h")

    writer = function_text(logger, "static void writer_task(", "logger")
    require_contains(
        writer,
        "xQueueReceive(s_write_queue, &line, 0)",
        "writer_task",
    )
    require_contains(
        writer,
        "lines_since_flush == 0U\n                ? portMAX_DELAY",
        "writer_task empty-queue park",
    )
    require_contains(
        writer,
        "pdMS_TO_TICKS(LUNASHELL_LOG_FLUSH_DEADLINE_MS)",
        "writer_task pending-flush deadline",
    )
    require_contains(
        writer,
        "write_queued_lines_watched(",
        "writer_task batched I/O watch",
    )
    disabled_at = writer.find("if (!sd_status.enabled)")
    disabled_drain_at = writer.find(
        "while (xQueueReceive(s_write_queue, &line, 0) == pdTRUE)",
        disabled_at,
    )
    idle_ack_at = writer.find("xSemaphoreGive(s_writer_idle)", disabled_at)
    require(
        disabled_at >= 0
        and disabled_drain_at > disabled_at
        and idle_ack_at > disabled_drain_at,
        "disabled writer acknowledged stop before draining admitted lines",
    )
    write_batch = function_text(
        logger,
        "static bool write_queued_lines_watched(",
        "logger",
    )
    require_contains(
        logger,
        "#define LUNASHELL_LOG_IO_BATCH_LINES 8U",
        "logger bounded batch policy",
    )
    require_contains(
        write_batch,
        "processed_lines < LUNASHELL_LOG_IO_BATCH_LINES",
        "bounded logger write batch",
    )
    require(
        "while (true)" not in write_batch,
        "logger write batch can again drain an indefinitely replenished queue",
    )
    require(
        "xQueueReceive(s_write_queue, &line, pdMS_TO_TICKS" not in logger,
        "logger writer regained periodic queue polling",
    )
    queue_capture = function_text(
        logger,
        "static int LUNASHELL_log_vprintf(",
        "logger",
    )
    require_contains(
        queue_capture,
        'notify_writer_state_change("queued-data")',
        "queued log wake",
    )
    require_contains(
        queue_capture,
        "begin_sd_enqueue(&sd_status, &sd_generation)",
        "SD enqueue admission barrier",
    )
    require_contains(
        queue_capture,
        "end_sd_enqueue();",
        "SD enqueue ownership release",
    )
    queue_failure_at = queue_capture.find(
        "record_sd_failure_for_generation(\n                \"queue\""
    )
    queue_notification_failure_at = queue_capture.find(
        "record_sd_failure_for_generation(\n                \"queue notification\""
    )
    queue_release_at = queue_capture.find(
        "end_sd_enqueue();",
        queue_notification_failure_at,
    )
    require(
        0 <= queue_failure_at < queue_notification_failure_at < queue_release_at,
        "producer failure publication must remain inside SD enqueue ownership",
    )
    format_slot_at = queue_capture.find("if (line == NULL)")
    format_admission_at = queue_capture.find(
        "begin_sd_enqueue(&sd_status, &sd_generation)",
        format_slot_at,
    )
    format_failure_at = queue_capture.find(
        "record_sd_failure_for_generation(",
        format_admission_at,
    )
    format_release_at = queue_capture.find(
        "end_sd_enqueue();",
        format_failure_at,
    )
    require(
        0 <= format_slot_at < format_admission_at < format_failure_at <
        format_release_at < queue_failure_at,
        "format-slot failure must use the same generation-bound ownership "
        "barrier as queued producers",
    )
    generation_failure = function_text(
        logger,
        "static bool record_sd_failure_impl(",
        "generation-bound SD failure commit",
    )
    require_contains(
        generation_failure,
        "expected_generation == current_generation",
        "stale SD producer rejection",
    )
    require(
        generation_failure.find("expected_generation == current_generation") <
        generation_failure.find("s_sd_status.enabled = false"),
        "an old SD generation can disable a new mirror lease",
    )
    enqueue_begin = function_text(
        logger,
        "static bool begin_sd_enqueue(",
        "logger",
    )
    require(
        enqueue_begin.count("s_sd_enqueue_open") >= 2,
        "SD enqueue admission lost its close-race double check",
    )
    require_contains(
        enqueue_begin,
        "memory_order_seq_cst",
        "SD enqueue barrier ordering",
    )
    gate_close = function_text(
        logger,
        "static esp_err_t close_sd_enqueue_gate(",
        "logger",
    )
    require_contains(
        gate_close,
        "s_sd_enqueue_inflight",
        "SD enqueue quiescence wait",
    )
    require_contains(
        gate_close,
        "vTaskDelay(1)",
        "bounded enqueue quiescence yield",
    )
    disable = function_text(logger, "static esp_err_t set_sd_disabled(", "logger")
    require(
        disable.find("close_sd_enqueue_gate()")
        < disable.find("s_sd_status.enabled = false"),
        "SD disabled state was published before admitted producers quiesced",
    )
    flush = function_text(logger, "static bool flush_log_file(", "logger")
    require(
        flush.find("fflush(*file)") < flush.find("note_persisted_write();"),
        "persisted-write sequence must advance only after a successful flush",
    )
    for fragment in (
        "begin_watched_io(",
        "end_watched_io(",
        "LUNASHELL_logger_set_io_watch(&kLoggerIoWatch)",
    ):
        require_contains(logger + storage, fragment, "logger/storage liveness")

    poll = function_text(storage, "esp_err_t poll_mounted_card(", "storage")
    require_contains(
        poll,
        '"runtime card status"',
        "cheap mounted-card health check",
    )
    require_contains(
        poll,
        "kStorageSpaceRefreshMs",
        "slow filesystem-space refresh",
    )
    require_contains(
        poll,
        "persisted_write_sequence",
        "write-triggered filesystem-space refresh",
    )
    require_contains(
        poll,
        "kStorageSpaceWriteRefreshMs",
        "coalesced write-triggered filesystem-space refresh",
    )
    require_contains(
        poll,
        "if (!refresh_space)",
        "filesystem-space early return",
    )
    require(
        poll.find("if (!refresh_space)") < poll.find("esp_vfs_fat_info("),
        "FAT space query must remain behind the refresh policy",
    )
    require(
        "publish_mount_report_locked" not in poll[: poll.find("esp_vfs_fat_info(")],
        "health-only poll fabricated a fresh filesystem-space timestamp",
    )
    require_contains(
        storage,
        "mounted ? poll_mounted_card(true) : ESP_OK",
        "power-resume forced filesystem validation",
    )
    require_contains(
        platform_header,
        "LUNASHELL_storage_request_space_refresh(void)",
        "public asynchronous Storage refresh API",
    )
    refresh_request = function_text(
        storage,
        'extern "C" esp_err_t LUNASHELL_storage_request_space_refresh(void)',
        "storage",
    )
    for fragment in (
        "s_terminal_failure",
        "s_power_suspended",
        "s_power_resume_requested",
        "!s_mounted",
        "s_space_refresh_requested = true",
        "xTaskNotifyGive(s_storage_task)",
    ):
        require_contains(
            refresh_request,
            fragment,
            "asynchronous Storage refresh request",
        )
    open_screen = function_text(
        app,
        "std::expected<void, std::string> App::open_screen(",
        "LUNASHELL app navigation",
    )
    storage_entry_at = open_screen.find("const bool entering_storage =")
    screen_commit_at = open_screen.find("screen_ = screen", storage_entry_at)
    storage_hook_at = open_screen.find(
        "if (entering_storage)",
        screen_commit_at,
    )
    mounted_gate_at = open_screen.find(
        "if (storage_entry_snapshot.storage.mounted)",
        storage_hook_at,
    )
    refresh_request_at = open_screen.find(
        "LUNASHELL_storage_request_space_refresh()",
        mounted_gate_at,
    )
    optional_error_at = open_screen.find(
        "if (space_refresh_result != ESP_OK)",
        refresh_request_at,
    )
    ui_refresh_at = open_screen.find(
        "if (auto refresh_result = refresh(context)",
        optional_error_at,
    )
    require(
        0 <= storage_entry_at < screen_commit_at < storage_hook_at
        < mounted_gate_at < refresh_request_at < optional_error_at
        < ui_refresh_at,
        "Storage screen entry must queue a mounted-card space refresh after "
        "navigation commits and before the first UI refresh",
    )
    storage_hook = open_screen[storage_hook_at:ui_refresh_at]
    require_contains(
        storage_hook,
        "ESP_LOGW(",
        "optional Storage entry refresh failure visibility",
    )
    require(
        "return " not in storage_hook,
        "an optional Storage entry refresh rejection must not fail navigation",
    )
    storage_task = function_text(storage, "void storage_task(", "storage")
    require_contains(
        storage_task,
        "poll_mounted_card(force_space_refresh)",
        "owner-task forced Storage refresh",
    )
    invalidate = function_text(
        storage,
        "esp_err_t invalidate_mounted_card(",
        "storage",
    )
    require_contains(
        invalidate,
        "if (logger_stop_error != ESP_OK)",
        "fail-closed logger shutdown before SD unmount",
    )
    require(
        "logger_stop_error == ESP_ERR_TIMEOUT" not in invalidate,
        "a non-timeout logger stop error can still permit an unsafe unmount",
    )
    require_contains(
        storage,
        "LUNASHELL_LIVENESS_STORAGE_FORMAT",
        "dedicated destructive-format watchdog domain",
    )
    require_contains(
        storage,
        "kStorageFormatWatchdogDeadlineMs = 12000",
        "reviewed destructive-format watchdog deadline",
    )
    logger_status = function_text(
        storage,
        "esp_err_t refresh_logger_status_locked(",
        "storage logger status reconciliation",
    )
    require_contains(
        logger_status,
        "action_error = status.last_error != ESP_OK\n"
        "            ? status.last_error\n"
        "            : ESP_ERR_INVALID_STATE;",
        "real logger error propagation before mismatch classification",
    )
    performance_transition = function_text(
        storage,
        'extern "C" esp_err_t LUNASHELL_storage_set_performance_suspended(bool suspended)',
        "Performance Mode storage transaction",
    )
    require(
        performance_transition.count("rollback_performance_storage_locked(") == 2,
        "Performance Mode storage must rollback both operation and status failures",
    )
    performance_rollback = function_text(
        storage,
        "esp_err_t rollback_performance_storage_locked(",
        "Performance Mode storage rollback",
    )
    for fragment in (
        "apply_effective_logger_mirror_locked()",
        "finish_logger_transition_rollback_locked(",
        '"Performance Mode"',
    ):
        require_contains(
            performance_rollback,
            fragment,
            "Performance Mode storage rollback",
        )
    shared_rollback = function_text(
        storage,
        "esp_err_t finish_logger_transition_rollback_locked(",
        "shared logger transition rollback",
    )
    for fragment in (
        "s_terminal_failure = true",
        "report.error = transition_error",
        "report.cleanup_error = rollback_error",
        "report.cleanup_complete = false",
        "report.poisoned = s_mounted",
        "return rollback_error",
    ):
        require_contains(
            shared_rollback,
            fragment,
            "shared logger terminal rollback",
        )
    power_transition = function_text(
        storage,
        'extern "C" esp_err_t LUNASHELL_storage_set_power_suspended(bool suspended)',
        "Power storage transaction",
    )
    require(
        power_transition.count("finish_logger_transition_rollback_locked(") == 2,
        "Power suspension must verify both transition and quiescence rollbacks",
    )
    require(
        power_transition.count('"Power"') >= 2,
        "Power suspension rollbacks lost their structured owner label",
    )
    require_contains(
        storage_task,
        'finish_logger_transition_rollback_locked(\n'
        '                                "Power",\n'
        '                                "resume transition"',
        "Power resume logger rollback",
    )
    resume_primary_at = storage_task.find(
        "const esp_err_t transition_error = resume_error;"
    )
    resume_restore_at = storage_task.find(
        "s_power_suspended = true;",
        resume_primary_at,
    )
    resume_rollback_at = storage_task.find(
        "apply_effective_logger_mirror_locked();",
        resume_restore_at,
    )
    resume_finish_at = storage_task.find(
        "finish_logger_transition_rollback_locked(",
        resume_rollback_at,
    )
    require(
        0 <= resume_primary_at < resume_restore_at < resume_rollback_at
        < resume_finish_at,
        "Power resume must restore its suspension reason before verifying the "
        "logger rollback under the storage lock",
    )
    for fragment in (
        "constexpr uint32_t kTraceVersion = 2U",
        "constexpr uint32_t kNoPoint = UINT32_MAX",
        "s_retained_timeout.last_progress_point == kNoPoint",
    ):
        require_contains(liveness, fragment, "stable retained liveness schema")

    require(writer_wait_ms(0) is None, "empty logger queue must park indefinitely")
    require(writer_wait_ms(1) == 100, "pending logger data lost its flush deadline")
    require(
        not enqueue_admitted(True, False),
        "producer preempted across gate close was incorrectly admitted",
    )
    require(
        enqueue_admitted(True, True),
        "producer inside an open gate was incorrectly rejected",
    )
    require(
        not enqueue_admitted(False, False),
        "closed enqueue gate admitted a new producer",
    )
    require(
        generation_bound_failure_commits(8, 8)
        and not generation_bound_failure_commits(8, 9),
        "generation-bound failure model can disable a newer SD lease",
    )
    require(
        transition_rollback_result(7, 0) == 7
        and transition_rollback_result(7, 9) == 9,
        "failed logger rollback did not supersede the primary transition error",
    )
    require(
        bounded_batch_count(48) == 8 and bounded_batch_count(3) == 3,
        "logger batch model is not bounded to eight lines",
    )
    require(
        drain_in_bounded_batches(48) == [8, 8, 8, 8, 8, 8],
        "stop drain did not preserve all accepted lines across bounded batches",
    )
    require(
        decode_retained_point(1, 11, 14) is None,
        "v1 no-point sentinel was reinterpreted after enum growth",
    )
    require(
        decode_retained_point(2, 0xFFFF_FFFF, 14) == 14,
        "v2 stable no-point sentinel was not decoded",
    )
    require(
        decode_retained_point(2, 11, 14) == 11,
        "v2 Storage liveness point was not decoded",
    )
    require(
        not filesystem_space_refresh_due(
            forced=False,
            tick_valid=True,
            elapsed_ms=59_999,
            persisted_sequence=7,
            observed_sequence=7,
        ),
        "unchanged filesystem space refreshed before its slow deadline",
    )
    require(
        filesystem_space_refresh_due(
            forced=False,
            tick_valid=True,
            elapsed_ms=9_999,
            persisted_sequence=8,
            observed_sequence=7,
        ) is False,
        "logger writes were not coalesced before the space-query deadline",
    )
    require(
        filesystem_space_refresh_due(
            forced=False,
            tick_valid=True,
            elapsed_ms=10_000,
            persisted_sequence=8,
            observed_sequence=7,
        ),
        "persisted logger write did not request a space refresh",
    )
    require(
        filesystem_space_refresh_due(
            forced=True,
            tick_valid=True,
            elapsed_ms=0,
            persisted_sequence=7,
            observed_sequence=7,
        ),
        "explicit validation did not force a space refresh",
    )

    print("storage/logger scheduling contract verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
