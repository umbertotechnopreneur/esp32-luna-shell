"""Deterministic, hardware-free checks for the DeviceSnapshot contract."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import copy


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components" / "qsee_platform" / "qsee_hardware.cpp"
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


def main() -> int:
    verify_source_contract()
    test_transaction_visibility()
    test_ttl_expiry()
    test_recovery_after_later_success()
    print("snapshot integrity verification: PASS (transaction visibility, TTL expiry, recovery)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
