"""Deterministic, hardware-free checks for the DeviceSnapshot contract."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import copy
import re


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components" / "qsee_platform" / "qsee_hardware.cpp"
MAIN_SOURCE = ROOT / "main" / "main.cpp"
HEADLESS_SOURCE = ROOT / "main" / "main_headless.cpp"
MEASURED_SAMPLE_TTL_US = 5_000_000

ERR_OK = 0
ERR_GENERIC = 1
ERR_NOT_FOUND = 2

DEVICE_HEALTH_UNKNOWN = "Unknown"
DEVICE_HEALTH_READY = "Ready"
DEVICE_HEALTH_DEGRADED = "Degraded"
DEVICE_HEALTH_MISSING = "Missing"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_contains(text: str, fragment: str, source: str) -> None:
    require(fragment in text, f"{fragment!r} missing from {source}")


def require_order(text: str, first: str, second: str, source: str) -> None:
    first_index = text.find(first)
    second_index = text.find(second)
    require(first_index >= 0, f"{first!r} missing from {source}")
    require(second_index >= 0, f"{second!r} missing from {source}")
    require(first_index < second_index, f"{first!r} must appear before {second!r} in {source}")


@dataclass
class ComponentSnapshotModel:
    health: str = DEVICE_HEALTH_UNKNOWN
    last_error: int = ERR_GENERIC
    data_valid: bool = False
    data_stale: bool = False
    last_attempt_at_us: int = 0
    last_success_at_us: int = 0


@dataclass
class AudioSnapshotModel:
    speaker: ComponentSnapshotModel = field(default_factory=ComponentSnapshotModel)
    microphone: ComponentSnapshotModel = field(default_factory=ComponentSnapshotModel)


@dataclass
class DeviceSnapshotModel:
    sequence: int = 0
    last_update_attempt_at_us: int = 0
    last_update_complete: bool = False
    last_update_error: int = ERR_GENERIC
    reason: str = "not-refreshed"
    battery: ComponentSnapshotModel = field(default_factory=ComponentSnapshotModel)
    imu: ComponentSnapshotModel = field(default_factory=ComponentSnapshotModel)
    sensor_fusion: ComponentSnapshotModel = field(default_factory=ComponentSnapshotModel)
    rtc: ComponentSnapshotModel = field(default_factory=ComponentSnapshotModel)
    gps: ComponentSnapshotModel = field(default_factory=ComponentSnapshotModel)
    audio: AudioSnapshotModel = field(default_factory=AudioSnapshotModel)
    memory: ComponentSnapshotModel = field(default_factory=ComponentSnapshotModel)
    processor: ComponentSnapshotModel = field(default_factory=ComponentSnapshotModel)


@dataclass
class SnapshotStoreModel:
    committed: DeviceSnapshotModel = field(default_factory=DeviceSnapshotModel)
    stage: DeviceSnapshotModel | None = None
    stage_dirty: bool = False


@dataclass
class DiagnosticReuseModel:
    committed_diagnostic_at_us: int = 0
    consumed_by_full_at_us: int = 0


def health_for(ready: bool, error: int) -> str:
    if ready:
        return DEVICE_HEALTH_READY
    return DEVICE_HEALTH_MISSING if error == ERR_NOT_FOUND else DEVICE_HEALTH_DEGRADED


def mark_sample_success(component: ComponentSnapshotModel, sampled_at_us: int) -> None:
    component.health = DEVICE_HEALTH_READY
    component.last_error = ERR_OK
    component.data_valid = True
    component.data_stale = False
    component.last_attempt_at_us = sampled_at_us
    component.last_success_at_us = sampled_at_us


def mark_sample_failure(component: ComponentSnapshotModel, error: int, attempted_at_us: int) -> None:
    component.health = health_for(False, error)
    component.last_error = error
    component.data_valid = False
    component.data_stale = component.last_success_at_us > 0
    component.last_attempt_at_us = attempted_at_us


def component_has_current_data(component: ComponentSnapshotModel) -> bool:
    return (
        component.health == DEVICE_HEALTH_READY
        and component.last_error == ERR_OK
        and component.data_valid
        and not component.data_stale
    )


def component_summary_state(component: ComponentSnapshotModel) -> str:
    if component_has_current_data(component):
        return "ready"
    if component.health == DEVICE_HEALTH_MISSING:
        return "missing"
    if component.last_error != ERR_OK or component.health == DEVICE_HEALTH_DEGRADED:
        return "error"
    if component.data_stale:
        return "stale"
    return "unavailable"


def age_measured_sample(component: ComponentSnapshotModel, now_us: int, ttl_us: int = MEASURED_SAMPLE_TTL_US) -> None:
    if not component.data_valid or component.data_stale or component.last_success_at_us <= 0:
        return
    if now_us < component.last_success_at_us or now_us - component.last_success_at_us >= ttl_us:
        component.data_valid = False
        component.data_stale = True


def apply_measured_sample_aging(snapshot: DeviceSnapshotModel, now_us: int) -> None:
    age_measured_sample(snapshot.battery, now_us)
    age_measured_sample(snapshot.rtc, now_us)
    age_measured_sample(snapshot.imu, now_us)
    age_measured_sample(snapshot.sensor_fusion, now_us)
    age_measured_sample(snapshot.gps, now_us)
    age_measured_sample(snapshot.audio.microphone, now_us)
    age_measured_sample(snapshot.memory, now_us)
    age_measured_sample(snapshot.processor, now_us)


def record_diagnostic_commit(
    reuse: DiagnosticReuseModel,
    component: ComponentSnapshotModel,
    commit_succeeded: bool,
) -> None:
    if (
        commit_succeeded
        and component_has_current_data(component)
        and component.last_attempt_at_us == component.last_success_at_us
    ):
        reuse.committed_diagnostic_at_us = component.last_success_at_us


def can_reuse_diagnostic_sample(
    component: ComponentSnapshotModel,
    reuse: DiagnosticReuseModel,
    now_us: int,
    reuse_window_us: int,
) -> bool:
    diagnostic_at_us = reuse.committed_diagnostic_at_us
    return (
        reuse_window_us > 0
        and component_has_current_data(component)
        and diagnostic_at_us > 0
        and component.last_attempt_at_us == component.last_success_at_us
        and component.last_success_at_us == diagnostic_at_us
        and diagnostic_at_us > reuse.consumed_by_full_at_us
        and now_us >= diagnostic_at_us
        and now_us - diagnostic_at_us < reuse_window_us
    )


def record_full_commit(
    reuse: DiagnosticReuseModel,
    reused_diagnostic_at_us: int,
    commit_succeeded: bool,
) -> None:
    if commit_succeeded and reused_diagnostic_at_us > reuse.consumed_by_full_at_us:
        reuse.consumed_by_full_at_us = reused_diagnostic_at_us


def begin_snapshot_stage(store: SnapshotStoreModel) -> DeviceSnapshotModel:
    if store.stage is not None:
        raise AssertionError("another stage is already active")
    store.stage = copy.deepcopy(store.committed)
    store.stage_dirty = False
    return store.stage


def commit_snapshot_stage(store: SnapshotStoreModel, kind: str = "full") -> DeviceSnapshotModel:
    if store.stage is None:
        raise AssertionError("stage ownership mismatch")
    if not store.stage_dirty:
        raise AssertionError("stage contains no updates")

    staged = store.stage
    committed = store.committed

    if kind == "full":
        if staged.battery.last_attempt_at_us > 0 and staged.battery.last_attempt_at_us >= committed.battery.last_attempt_at_us:
            committed.battery = copy.deepcopy(staged.battery)
        if staged.rtc.last_attempt_at_us > 0 and staged.rtc.last_attempt_at_us >= committed.rtc.last_attempt_at_us:
            committed.rtc = copy.deepcopy(staged.rtc)
        if staged.gps.last_attempt_at_us > 0 and staged.gps.last_attempt_at_us >= committed.gps.last_attempt_at_us:
            committed.gps = copy.deepcopy(staged.gps)
        if staged.memory.last_attempt_at_us > 0 and staged.memory.last_attempt_at_us >= committed.memory.last_attempt_at_us:
            committed.memory = copy.deepcopy(staged.memory)
        if staged.processor.last_attempt_at_us > 0 and staged.processor.last_attempt_at_us >= committed.processor.last_attempt_at_us:
            committed.processor = copy.deepcopy(staged.processor)

    if staged.imu.last_attempt_at_us > 0 and staged.imu.last_attempt_at_us >= committed.imu.last_attempt_at_us:
        committed.imu = copy.deepcopy(staged.imu)
    if staged.audio.microphone.last_attempt_at_us > 0 and staged.audio.microphone.last_attempt_at_us >= committed.audio.microphone.last_attempt_at_us:
        committed.audio.microphone = copy.deepcopy(staged.audio.microphone)

    committed.last_update_attempt_at_us = staged.last_update_attempt_at_us
    committed.last_update_complete = staged.last_update_complete
    committed.last_update_error = staged.last_update_error
    committed.reason = staged.reason
    committed.sequence += 1

    store.stage = None
    store.stage_dirty = False
    return committed


def verify_source_contract() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    main_source = MAIN_SOURCE.read_text(encoding="utf-8")
    headless_source = HEADLESS_SOURCE.read_text(encoding="utf-8")

    require_contains(source, "void mark_sample_success(", "qsee_hardware.cpp")
    require_contains(source, "void mark_sample_failure(", "qsee_hardware.cpp")
    require_contains(source, "void age_measured_sample(", "qsee_hardware.cpp")
    require_contains(source, "void apply_measured_sample_aging(", "qsee_hardware.cpp")
    require_contains(source, "constexpr int64_t kMeasuredSampleTtlUs = 5LL * 1000LL * 1000LL;", "qsee_hardware.cpp")

    require_contains(source, "apply_measured_sample_aging(copy, esp_timer_get_time());", "snapshot()")
    require_contains(source, "apply_measured_sample_aging(summary, esp_timer_get_time());", "refresh()")
    require_order(
        source,
        "copy = s_snapshot;",
        "apply_measured_sample_aging(copy, esp_timer_get_time());",
        "snapshot()",
    )
    require_order(
        source,
        "const esp_err_t commit_error = commit_snapshot_stage(",
        "apply_measured_sample_aging(summary, esp_timer_get_time());",
        "refresh()",
    )
    commit_start = source.index("esp_err_t commit_snapshot_stage(")
    merge_index = source.index("merge_snapshot_stage(s_snapshot, stage, kind);", commit_start)
    advance_index = source.index("advance_snapshot_sequence();", commit_start)
    require(
        merge_index < advance_index,
        "'merge_snapshot_stage(s_snapshot, stage, kind);' must appear before "
        "'advance_snapshot_sequence();' in commit_snapshot_stage()",
    )

    for fragment in (
        "age_measured_sample(snapshot.battery, now_us);",
        "age_measured_sample(snapshot.rtc, now_us);",
        "age_measured_sample(snapshot.imu, now_us);",
        "age_measured_sample(snapshot.sensor_fusion, now_us);",
        "age_measured_sample(snapshot.gps, now_us);",
        "age_measured_sample(snapshot.audio.microphone, now_us);",
        "age_measured_sample(snapshot.memory, now_us);",
        "age_measured_sample(snapshot.processor, now_us);",
    ):
        require_contains(source, fragment, "apply_measured_sample_aging()")

    require_contains(source, "component.data_stale = component.last_success_at_us > 0;", "mark_sample_failure()")
    require_contains(source, "component.data_stale = false;", "mark_sample_success()")

    for fragment in (
        "diagnostic_reuse_limit_ms",
        "s_last_committed_diagnostic_imu_us",
        "s_last_committed_diagnostic_microphone_us",
        "s_last_full_consumed_diagnostic_imu_us",
        "s_last_full_consumed_diagnostic_microphone_us",
        "qsee_hardware_set_diagnostic_streams(",
    ):
        require_contains(source, fragment, "Diagnostic-to-Full reuse contract")

    diagnostic_task_start = source.index("void diagnostic_monitor_task(")
    diagnostic_refresh_lock_index = source.index(
        "xSemaphoreTake(refresh_mutex",
        diagnostic_task_start,
    )
    diagnostic_microphone_recheck_index = source.index(
        "s_diagnostic_microphone_active.load(",
        diagnostic_refresh_lock_index,
    )
    diagnostic_microphone_sample_index = source.index(
        'sample_microphone("diagnostic-ui")',
        diagnostic_microphone_recheck_index,
    )
    require(
        diagnostic_refresh_lock_index
        < diagnostic_microphone_recheck_index
        < diagnostic_microphone_sample_index,
        "Diagnostic microphone enable state must be re-checked under the "
        "refresh lock before capture",
    )

    stream_setter_start = source.index(
        'extern "C" esp_err_t qsee_hardware_set_diagnostic_streams('
    )
    stream_setter_lock_index = source.index(
        "xSemaphoreTake(refresh_mutex",
        stream_setter_start,
    )
    stream_setter_store_index = source.index(
        "s_diagnostic_microphone_active.store(",
        stream_setter_lock_index,
    )
    stream_setter_unlock_index = source.index(
        "give_refresh_mutex(",
        stream_setter_store_index,
    )
    require(
        stream_setter_lock_index
        < stream_setter_store_index
        < stream_setter_unlock_index,
        "Diagnostic stream selection must update microphone state inside the "
        "refresh-lock quiescence barrier",
    )

    diagnostic_commit_index = source.index(
        "SnapshotStageKind::Diagnostic",
        diagnostic_task_start,
    )
    diagnostic_unlock_index = source.index(
        "give_refresh_mutex(",
        diagnostic_commit_index,
    )
    diagnostic_imu_marker_index = source.index(
        "s_last_committed_diagnostic_imu_us =",
        diagnostic_commit_index,
    )
    diagnostic_microphone_marker_index = source.index(
        "s_last_committed_diagnostic_microphone_us =",
        diagnostic_commit_index,
    )
    require(
        diagnostic_commit_index
        < diagnostic_imu_marker_index
        < diagnostic_unlock_index,
        "Diagnostic IMU provenance must be recorded after commit and before "
        "releasing the refresh lock",
    )
    require(
        diagnostic_commit_index
        < diagnostic_microphone_marker_index
        < diagnostic_unlock_index,
        "Diagnostic microphone provenance must be recorded after commit and "
        "before releasing the refresh lock",
    )

    refresh_start = source.index("esp_err_t DeviceSnapshotService::refresh(")
    full_commit_index = source.index(
        "SnapshotStageKind::FullRefresh",
        refresh_start,
    )
    full_unlock_index = source.index(
        'give_refresh_mutex(refresh_mutex, "Hardware refresh")',
        full_commit_index,
    )
    full_imu_marker_index = source.index(
        "s_last_full_consumed_diagnostic_imu_us =",
        full_commit_index,
    )
    full_microphone_marker_index = source.index(
        "s_last_full_consumed_diagnostic_microphone_us =",
        full_commit_index,
    )
    require(
        full_commit_index < full_imu_marker_index < full_unlock_index,
        "Full IMU consumption must be recorded after commit and before "
        "releasing the refresh lock",
    )
    require(
        full_commit_index
        < full_microphone_marker_index
        < full_unlock_index,
        "Full microphone consumption must be recorded after commit and before "
        "releasing the refresh lock",
    )

    registration = re.search(
        r"heap_caps_register_failed_alloc_callback\s*\(\s*&?([A-Za-z_]\w*)\s*\)",
        source,
    )
    require(registration is not None, "heap allocation failure callback is not registered")
    require_contains(
        source,
        '#include "xt_utils.h"',
        "native Xtensa compare-and-set helper",
    )
    require_contains(
        source,
        "static_assert(sizeof(size_t) == sizeof(uint32_t));",
        "32-bit allocation callback size contract",
    )
    require_contains(
        source,
        "xt_utils_compare_and_set(",
        "native lock-free allocation failure counters",
    )
    for counter_name in (
        "s_allocation_failure_count",
        "s_largest_failed_allocation_bytes",
        "s_failed_allocation_caps_seen_mask",
    ):
        require_contains(
            source,
            f"DRAM_ATTR volatile uint32_t {counter_name}",
            f"internal-DRAM allocation failure counter {counter_name}",
        )

    counter_helpers_start = source.index(
        "FORCE_INLINE_ATTR void allocation_counter_update_max("
    )
    counter_helpers_end = source.index(
        "void IRAM_ATTR allocation_failed_hook(",
        counter_helpers_start,
    )
    counter_helpers = source[counter_helpers_start:counter_helpers_end]
    require(
        counter_helpers.count("xt_utils_compare_and_set(") == 3,
        "allocation failure helpers must use one native CAS loop per aggregate",
    )

    for entry_name, entry_source in (
        ("display app_main", main_source),
        ("headless app_main", headless_source),
    ):
        telemetry_init_index = entry_source.index(
            "ESP_ERROR_CHECK(qsee_memory_telemetry_init());"
        )
        logger_init_index = entry_source.index(
            "ESP_ERROR_CHECK(qsee_logger_init());"
        )
        require(
            telemetry_init_index < logger_init_index,
            f"{entry_name} must register heap telemetry before the logger",
        )

    callback_name = registration.group(1)
    definition = re.search(
        rf"(?P<signature>(?:static\s+)?(?:IRAM_ATTR\s+)?void"
        rf"(?:\s+IRAM_ATTR)?\s+{re.escape(callback_name)}\s*\([^)]*\))\s*\{{",
        source,
    )
    require(definition is not None, f"failed-allocation callback {callback_name!r} is not defined")
    require(
        "IRAM_ATTR" in definition.group("signature"),
        "failed-allocation callback must be IRAM_ATTR",
    )

    body_start = definition.end() - 1
    depth = 0
    body_end = -1
    for index in range(body_start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                body_end = index
                break
    require(body_end > body_start, "failed-allocation callback body is incomplete")
    callback_body = source[body_start : body_end + 1]
    allocation_hook_source = counter_helpers + callback_body
    for forbidden in (
        "ESP_LOG",
        "update_snapshot(",
        "esp_timer_get_time(",
        "xSemaphore",
        "std::mutex",
        "lock_guard",
        "strlcpy(",
        "snprintf(",
        "printf(",
        "malloc(",
        "calloc(",
        "realloc(",
    ):
        require(
            forbidden not in allocation_hook_source,
            f"failed-allocation callback performs forbidden work: {forbidden}",
        )


def test_transaction_visibility() -> None:
    store = SnapshotStoreModel()
    store.committed.sequence = 7
    store.committed.reason = "boot"
    mark_sample_success(store.committed.battery, 1_000)

    stage = begin_snapshot_stage(store)
    mark_sample_success(stage.battery, 2_000)
    stage.last_update_attempt_at_us = 2_000
    stage.last_update_complete = True
    stage.last_update_error = ERR_OK
    stage.reason = "refresh"
    store.stage_dirty = True

    require(store.committed.battery.last_success_at_us == 1_000, "committed snapshot changed before commit")
    require(store.committed.sequence == 7, "sequence changed before commit")
    require(store.committed.reason == "boot", "reason changed before commit")

    committed = commit_snapshot_stage(store)
    require(committed.battery.last_success_at_us == 2_000, "staged battery sample was not committed")
    require(committed.sequence == 8, "sequence did not advance exactly once on commit")
    require(committed.reason == "refresh", "committed reason did not match staged reason")
    require(store.stage is None, "stage was not cleared after commit")


def test_ttl_expiry() -> None:
    fresh = ComponentSnapshotModel()
    mark_sample_success(fresh, 1_000_000)
    age_measured_sample(fresh, 1_000_000 + MEASURED_SAMPLE_TTL_US - 1)
    require(component_has_current_data(fresh), "sample expired too early")
    require(component_summary_state(fresh) == "ready", "fresh sample summary should stay ready")

    expired = ComponentSnapshotModel()
    mark_sample_success(expired, 1_000_000)
    age_measured_sample(expired, 1_000_000 + MEASURED_SAMPLE_TTL_US)
    require(not expired.data_valid, "expired sample remained valid")
    require(expired.data_stale, "expired sample was not marked stale")
    require(component_summary_state(expired) == "stale", "expired sample summary should be stale")

    rewound = ComponentSnapshotModel()
    mark_sample_success(rewound, 1_000_000)
    age_measured_sample(rewound, 999_999)
    require(not rewound.data_valid, "time regression should invalidate the sample")
    require(rewound.data_stale, "time regression should mark the sample stale")


def test_recovery_after_later_success() -> None:
    component = ComponentSnapshotModel()
    mark_sample_success(component, 1_000_000)
    mark_sample_failure(component, ERR_GENERIC, 2_000_000)
    require(not component.data_valid, "failed sample should not stay valid")
    require(component.data_stale, "failed sample after a success should be stale")
    require(component_summary_state(component) == "error", "failed sample should summarize as error")

    mark_sample_success(component, 3_000_000)
    require(component.data_valid, "later success should restore validity")
    require(not component.data_stale, "later success should clear stale state")
    require(component.last_success_at_us == 3_000_000, "later success timestamp was not recorded")
    require(component_summary_state(component) == "ready", "later success should recover the summary state")


def test_diagnostic_reuse_is_fresh_and_one_shot() -> None:
    component = ComponentSnapshotModel()
    mark_sample_success(component, 1_000_000)
    reuse = DiagnosticReuseModel()
    record_diagnostic_commit(reuse, component, commit_succeeded=True)

    original_attempt = component.last_attempt_at_us
    original_success = component.last_success_at_us
    require(
        can_reuse_diagnostic_sample(
            component,
            reuse,
            now_us=1_100_000,
            reuse_window_us=300_000,
        ),
        "fresh committed Diagnostic sample was not reusable",
    )
    record_full_commit(
        reuse,
        reused_diagnostic_at_us=reuse.committed_diagnostic_at_us,
        commit_succeeded=True,
    )
    require(
        component.last_attempt_at_us == original_attempt
        and component.last_success_at_us == original_success,
        "reusing a Diagnostic sample must not advance its measurement timestamps",
    )
    require(
        not can_reuse_diagnostic_sample(
            component,
            reuse,
            now_us=1_200_000,
            reuse_window_us=300_000,
        ),
        "one Diagnostic sample was consumed by more than one Full generation",
    )


def test_diagnostic_reuse_rejects_invalid_or_old_samples() -> None:
    fresh = ComponentSnapshotModel()
    mark_sample_success(fresh, 2_000_000)
    reuse = DiagnosticReuseModel()
    record_diagnostic_commit(reuse, fresh, commit_succeeded=True)

    require(
        not can_reuse_diagnostic_sample(
            fresh,
            reuse,
            now_us=2_300_000,
            reuse_window_us=300_000,
        ),
        "sample at the reuse-window boundary must be reacquired",
    )
    require(
        not can_reuse_diagnostic_sample(
            fresh,
            reuse,
            now_us=1_999_999,
            reuse_window_us=300_000,
        ),
        "time regression must reject Diagnostic reuse",
    )
    require(
        not can_reuse_diagnostic_sample(
            fresh,
            reuse,
            now_us=2_100_000,
            reuse_window_us=0,
        ),
        "manual or boot refresh with a zero window reused Diagnostic data",
    )

    stale = copy.deepcopy(fresh)
    stale.data_valid = False
    stale.data_stale = True
    require(
        not can_reuse_diagnostic_sample(
            stale,
            reuse,
            now_us=2_100_000,
            reuse_window_us=300_000,
        ),
        "stale Diagnostic sample was reused",
    )

    failed = copy.deepcopy(fresh)
    mark_sample_failure(failed, ERR_GENERIC, 2_050_000)
    require(
        not can_reuse_diagnostic_sample(
            failed,
            reuse,
            now_us=2_100_000,
            reuse_window_us=300_000,
        ),
        "failed Diagnostic sample was reused",
    )

    replaced_by_full = copy.deepcopy(fresh)
    mark_sample_success(replaced_by_full, 2_050_000)
    require(
        not can_reuse_diagnostic_sample(
            replaced_by_full,
            reuse,
            now_us=2_100_000,
            reuse_window_us=300_000,
        ),
        "sample whose timestamp came from FullRefresh was mistaken for Diagnostic",
    )


def test_diagnostic_reuse_requires_successful_commits() -> None:
    component = ComponentSnapshotModel()
    mark_sample_success(component, 3_000_000)
    reuse = DiagnosticReuseModel()
    record_diagnostic_commit(reuse, component, commit_succeeded=False)
    require(
        reuse.committed_diagnostic_at_us == 0,
        "failed Diagnostic commit published provenance",
    )
    require(
        not can_reuse_diagnostic_sample(
            component,
            reuse,
            now_us=3_100_000,
            reuse_window_us=300_000,
        ),
        "sample from a failed Diagnostic commit was reused",
    )

    record_diagnostic_commit(reuse, component, commit_succeeded=True)
    require(
        can_reuse_diagnostic_sample(
            component,
            reuse,
            now_us=3_100_000,
            reuse_window_us=300_000,
        ),
        "successfully committed Diagnostic sample was not reusable",
    )
    record_full_commit(
        reuse,
        reused_diagnostic_at_us=reuse.committed_diagnostic_at_us,
        commit_succeeded=False,
    )
    require(
        reuse.consumed_by_full_at_us == 0,
        "failed Full commit consumed Diagnostic provenance",
    )
    require(
        can_reuse_diagnostic_sample(
            component,
            reuse,
            now_us=3_150_000,
            reuse_window_us=300_000,
        ),
        "failed Full commit prevented a later safe reuse",
    )


def main() -> int:
    verify_source_contract()
    test_transaction_visibility()
    test_ttl_expiry()
    test_recovery_after_later_success()
    test_diagnostic_reuse_is_fresh_and_one_shot()
    test_diagnostic_reuse_rejects_invalid_or_old_samples()
    test_diagnostic_reuse_requires_successful_commits()
    print(
        "snapshot integrity verification: PASS "
        "(transaction visibility, TTL, recovery, one-shot Diagnostic reuse, "
        "stream quiescence, heap-hook safety)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
