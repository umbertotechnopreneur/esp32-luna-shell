"""Hardware-free contract checks for dual TOF400F precision distance."""

from __future__ import annotations

import json
from pathlib import Path
from math import cos, radians
from statistics import median
from typing import Any

from firmware_profile import read_profile_defaults


ROOT = Path(__file__).resolve().parents[1]
CALIBRATION_TRANSPORT_FAILURE_THRESHOLD = 3
CALIBRATION_DEADLINE_MARGIN_MS = 30_000


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_contains(path: Path, fragment: str) -> None:
    text = path.read_text(encoding="utf-8")
    require(fragment in text, f"{fragment!r} missing from {path}")


def child_with_id(parent: dict[str, Any], child_id: str, path: str) -> dict[str, Any]:
    matches = [
        child
        for child in parent.get("children", [])
        if isinstance(child, dict) and child.get("id") == child_id
    ]
    require(
        len(matches) == 1,
        f"{path} must contain exactly one child with id {child_id!r}",
    )
    return matches[0]


def screen_with_id(document: dict[str, Any], screen_id: str) -> dict[str, Any]:
    matches = [
        asset
        for asset in document.get("assets", [])
        if isinstance(asset, dict)
        and asset.get("type") == "viewScreen"
        and asset.get("id") == screen_id
    ]
    require(
        len(matches) == 1,
        f"gui/root.json must contain exactly one viewScreen {screen_id!r}",
    )
    return matches[0]


def dp_value(value: Any, path: str) -> float:
    if isinstance(value, (int, float)):
        return float(value)
    require(
        isinstance(value, str) and value.endswith("dp"),
        f"{path} must be a numeric dp value, got {value!r}",
    )
    try:
        return float(value[:-2])
    except ValueError as error:
        raise AssertionError(
            f"{path} must be a numeric dp value, got {value!r}"
        ) from error


def require_minimum_height(
    node: dict[str, Any],
    minimum_dp: float,
    path: str,
) -> None:
    height = dp_value(node.get("placement", {}).get("height"), f"{path}.height")
    require(
        height >= minimum_dp,
        f"{path}.height must be at least {minimum_dp:g}dp, got {height:g}dp",
    )


def require_wrap_height(node: dict[str, Any], path: str) -> None:
    height = node.get("placement", {}).get("height")
    require(
        height == "wrap",
        f"{path}.height must be 'wrap', got {height!r}",
    )


def verify_tof_layout(gui_root: Path) -> None:
    document = json.loads(gui_root.read_text(encoding="utf-8"))

    tof = screen_with_id(document, "LUNASHELL_tof")
    tof_surface = child_with_id(tof, "surface", "LUNASHELL_tof")
    require_minimum_height(
        child_with_id(tof_surface, "title", "LUNASHELL_tof/surface"),
        28,
        "LUNASHELL_tof/surface/title",
    )
    require_minimum_height(
        child_with_id(tof_surface, "status", "LUNASHELL_tof/surface"),
        32,
        "LUNASHELL_tof/surface/status",
    )
    require_minimum_height(
        child_with_id(tof_surface, "calibration", "LUNASHELL_tof/surface"),
        32,
        "LUNASHELL_tof/surface/calibration",
    )

    layout_budget_dp = 320.0
    for child in tof_surface.get("children", []):
        if not isinstance(child, dict):
            continue
        placement = child.get("placement", {})
        if placement.get("mode") != "absolute":
            continue
        child_path = f"LUNASHELL_tof/surface/{child.get('id', '<unnamed>')}"
        top = dp_value(placement.get("y", 0), f"{child_path}.y")
        height = dp_value(placement.get("height"), f"{child_path}.height")
        require(top >= 0, f"{child_path}.y must not be negative")
        require(
            top + height <= layout_budget_dp,
            f"{child_path} ends at {top + height:g}dp, beyond the "
            f"{layout_budget_dp:g}dp screen budget",
        )

    tof_settings = screen_with_id(document, "LUNASHELL_tof_settings")
    settings_surface = child_with_id(
        tof_settings,
        "surface",
        "LUNASHELL_tof_settings",
    )
    scroller = child_with_id(
        settings_surface,
        "tof_settings_scroller",
        "LUNASHELL_tof_settings/surface",
    )
    require_minimum_height(
        child_with_id(
            scroller,
            "title",
            "LUNASHELL_tof_settings/surface/tof_settings_scroller",
        ),
        28,
        "LUNASHELL_tof_settings/surface/tof_settings_scroller/title",
    )
    for child_id in (
        "settings_status",
        "calibration_status",
        "calibration_hint",
    ):
        require_wrap_height(
            child_with_id(
                scroller,
                child_id,
                "LUNASHELL_tof_settings/surface/tof_settings_scroller",
            ),
            f"LUNASHELL_tof_settings/surface/tof_settings_scroller/{child_id}",
        )


def robust_mean(values: list[int]) -> tuple[int, int, int]:
    ordered = sorted(values)
    center = int(median(ordered))
    deviations = sorted(abs(value - center) for value in ordered)
    mad = int(median(deviations))
    threshold = max(20, mad * 3)
    inliers = [value for value in ordered if abs(value - center) <= threshold]
    minimum = max(20, (len(values) * 80 + 99) // 100)
    require(len(inliers) >= minimum, "test vector unexpectedly lacks inliers")
    return round(sum(inliers) / len(inliers)), mad, len(inliers)


def synchronized_acquisition(
    requested_at_us: int,
    tof_sampled_at_us: tuple[int, int],
    imu_sampled_at_us: int,
    fusion_sampled_at_us: int,
) -> bool:
    timestamps = (
        requested_at_us,
        *tof_sampled_at_us,
        imu_sampled_at_us,
        fusion_sampled_at_us,
    )
    if any(timestamp <= 0 for timestamp in timestamps):
        return False
    if abs(tof_sampled_at_us[0] - tof_sampled_at_us[1]) > 50_000:
        return False
    if abs(imu_sampled_at_us - fusion_sampled_at_us) > 50_000:
        return False
    geometry_skew_us = max(
        abs(tof_timestamp - geometry_timestamp)
        for tof_timestamp in (requested_at_us, *tof_sampled_at_us)
        for geometry_timestamp in (imu_sampled_at_us, fusion_sampled_at_us)
    )
    return geometry_skew_us <= 500_000


def recovery_backoff_ms(consecutive_failures: int) -> int:
    if consecutive_failures <= 0:
        return 0
    return min(1_000 << min(consecutive_failures - 1, 5), 30_000)


def calibration_deadline_us(
    started_at_us: int,
    sample_count: int,
    interval_ms: int,
) -> int:
    budget_ms = (
        2 * sample_count * interval_ms + CALIBRATION_DEADLINE_MARGIN_MS
    )
    return started_at_us + budget_ms * 1_000


def calibration_terminal_result(
    previous_record_valid: bool,
    consecutive_transport_failures: int,
    transport_error: str,
    now_us: int,
    deadline_us: int,
) -> tuple[str, str | None]:
    if (
        consecutive_transport_failures
        >= CALIBRATION_TRANSPORT_FAILURE_THRESHOLD
    ):
        return (
            "ready" if previous_record_valid else "failed",
            transport_error,
        )
    if now_us >= deadline_us:
        return (
            "ready" if previous_record_valid else "failed",
            "ESP_ERR_TIMEOUT",
        )
    return "running", None


def settings_mutation_allowed(
    calibration_state: str,
    calibration_requested: bool = False,
) -> bool:
    return (
        calibration_state != "running"
        and not calibration_requested
    )


def rate_limited_warning_emissions(
    events: list[tuple[str, int]],
) -> list[tuple[str, int, int]]:
    last_emitted_at_us: dict[str, int] = {}
    suppressed: dict[str, int] = {}
    emitted: list[tuple[str, int, int]] = []
    for category, now_us in events:
        last_us = last_emitted_at_us.get(category)
        if (
            last_us is None
            or now_us < last_us
            or now_us - last_us >= 10_000_000
        ):
            emitted.append((category, now_us, suppressed.get(category, 0)))
            last_emitted_at_us[category] = now_us
            suppressed[category] = 0
        else:
            suppressed[category] = suppressed.get(category, 0) + 1
    return emitted


def main() -> int:
    driver = ROOT / "components/LUNASHELL_tof400f/include/LUNASHELL_tof400f.h"
    driver_cmake = ROOT / "components/LUNASHELL_tof400f/CMakeLists.txt"
    build_kconfig = ROOT / "main/Kconfig.projbuild"
    service = ROOT / "components/LUNASHELL_platform/LUNASHELL_distance_service.cpp"
    snapshot = ROOT / "components/LUNASHELL_platform/include/LUNASHELL_device_snapshot.hpp"
    hardware = ROOT / "components/LUNASHELL_platform/LUNASHELL_hardware.cpp"
    wiring = ROOT / "docs/hardware/tof400f-dual-uart.md"
    sdkconfig_defaults = read_profile_defaults()
    gui_root = ROOT / "components/LUNASHELL_app/gui/root.json"

    for fragment in (
        "#define LUNASHELL_TOF400F_SHARED_TX_GPIO 16",
        "#define LUNASHELL_TOF400F_SENSOR_1_RX_GPIO 17",
        "#define LUNASHELL_TOF400F_SENSOR_2_RX_GPIO 18",
        "int64_t requested_at_us",
        "LUNASHELL_tof400f_deinit(void)",
    ):
        require_contains(driver, fragment)

    for fragment in (
        "CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE AND",
        "CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART",
        'SRCS\n            "LUNASHELL_tof400f.c"',
        "idf_component_optional_requires(PUBLIC esp_common)",
        "esp_driver_uart\n        esp_timer\n        freertos\n        log",
        "else()\n    idf_component_register()\nendif()",
    ):
        require_contains(driver_cmake, fragment)

    require_contains(
        build_kconfig,
        "config LUNASHELL_BUILD_ENABLE_DISTANCE\n"
        '    bool "Include external precision distance support"\n'
        "    default n",
    )
    require(
        "# CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE is not set"
        in sdkconfig_defaults,
        "standard full profile must keep distance support disabled",
    )

    for fragment in (
        "kDefaultSampleCount = LUNASHELL::kDistanceMaximumSampleCount",
        "interval_ms == 250U || interval_ms == 500U",
        "static_cast<uint32_t>(estimate.mad_mm) * 3U",
        "s_calibration_windows",
        "s_calibration_offset_windows",
        "s_runtime_vertical_windows",
        "robust_signed_estimate(",
        "settings.setBlob(",
        "current_imu_geometry(",
        "sampleDistanceGeometry(&geometry)",
        "kMaximumTofPairSkewUs = 50000U",
        "kMaximumImuFusionSkewUs = 50000U",
        "kMaximumTofGeometrySkewUs = 500000U",
        "pair.requested_at_us",
        "spread_mm > allowed_spread_mm",
        "delay_until_next_interval(",
        "sensor.window_sample_count = 0",
        "settings_error",
        "mark_settings_persistence_failure_locked(",
        "LUNASHELL_tof400f_deinit()",
        "uxTaskGetStackHighWaterMark(nullptr)",
        "TOF service is running but its transport is unavailable",
        "s_distance.last_error != ESP_OK",
        "sensor.protocol_error_count = result.protocol_error_count",
        "sensor.uart_error_count = result.uart_error_count",
        "kRecoveryMaximumBackoffMs = 30000U",
        "s_calibration_offset_windows[index].clear()",
        "kCalibrationRecordVersion = 2",
        "LegacyCalibrationRecordV1",
        "calibration_record_crc32(record)",
        "s_pending_calibration_z_axis_sign",
        "measured_z_axis_sign != s_calibration_z_axis_sign",
        "s_calibration_requirement_error = ESP_ERR_NOT_SUPPORTED",
        "kWorkerWarningIntervalUs = 10'000'000",
        "enum class WorkerWarningCategory",
        "struct WorkerWarningState",
        "s_worker_warning_states{};",
        "WorkerWarningCategory::GeometryFailure",
        "WorkerWarningCategory::RuntimeTiming",
        "WorkerWarningCategory::RuntimePosture",
        "WorkerWarningCategory::RuntimeSign",
        "WorkerWarningCategory::RuntimeRange",
        "WorkerWarningCategory::CalibrationTiming",
        "WorkerWarningCategory::CalibrationPosture",
        "WorkerWarningCategory::CalibrationSign",
        "WorkerWarningCategory::CalibrationRange",
        "now_us - state.last_emitted_at_us >= kWorkerWarningIntervalUs",
        "increment_saturated(&state.suppressed)",
        "kCalibrationTransportFailureThreshold = 3U",
        "kCalibrationDeadlineMarginMs = 30000U",
        "s_calibration_consecutive_transport_failures",
        "s_calibration_deadline_at_us = calibration_deadline_at_us(",
        "transport_failure_limit_reached",
        "s_calibration_consecutive_transport_failures >=",
        "evaluated_at_us >= s_calibration_deadline_at_us",
        "esp_err_to_name(transport_error)",
    ):
        require_contains(service, fragment)
    service_text = service.read_text(encoding="utf-8")
    require(
        service_text.count("EXT_RAM_BSS_ATTR") >= 4,
        "all four large TOF windows must live in external RAM",
    )
    require(
        ".snapshot()" not in service_text,
        "TOF service regressed to copying the complete DeviceSnapshot",
    )
    require(
        "sizeof(StackType_t)" not in service_text,
        "ESP-IDF 6 stack high-water telemetry is already reported in bytes",
    )
    require(
        service_text.count("suppressed=%lu") >= 10,
        "repeatable worker warnings must report the suppressed count",
    )
    require(
        service_text.count(
            "increment_saturated(&s_distance.timing_rejection_count);"
        )
        == 2,
        "runtime and calibration timing counters must increment per rejection",
    )
    require(
        service_text.count(
            "increment_saturated(&s_distance.rejected_pair_count);"
        )
        == 4,
        "pair rejection counters must remain independent of warning emission",
    )
    require(
        "++s_calibration_rejected[index];" in service_text,
        "calibration rejection counters must increment for every rejected pair",
    )
    failure_start = service_text.index(
        "void finish_calibration_failure_locked("
    )
    failure_end = service_text.index(
        "\nvoid process_calibration_sample(",
        failure_start,
    )
    failure_body = service_text[failure_start:failure_end]
    require(
        "DistanceCalibrationState::Ready" in failure_body
        and "DistanceCalibrationState::Failed" in failure_body,
        "calibration failure must leave Running based on prior record validity",
    )
    require(
        "s_calibration_deadline_at_us = 0;" in failure_body
        and "s_calibration_consecutive_transport_failures = 0;"
        in failure_body,
        "calibration failure must clear terminal bookkeeping",
    )
    setter_names = (
        "distanceSetSampleCount",
        "distanceSetSampleIntervalMs",
        "distanceSetReferenceMm",
    )
    for index, setter_name in enumerate(setter_names):
        setter_start = service_text.index(f"esp_err_t {setter_name}(")
        if index + 1 < len(setter_names):
            setter_end = service_text.index(
                f"\nesp_err_t {setter_names[index + 1]}(",
                setter_start,
            )
        else:
            setter_end = service_text.index(
                "\n}  // namespace LUNASHELL",
                setter_start,
            )
        setter_body = service_text[setter_start:setter_end]
        require(
            "DistanceCalibrationState::Running" in setter_body
            and "s_calibration_requested" in setter_body,
            f"{setter_name} must reject only active/requested calibration",
        )

    for fragment in (
        "kDistanceMinimumSampleCount = 20",
        "kDistanceMaximumSampleCount = 500",
        "kDistanceMinimumReferenceMm = 500",
        "kDistanceMaximumReferenceMm = 1500",
        "bool record_valid = false",
        "uint32_t protocol_error_count = 0",
        "uint32_t uart_error_count = 0",
        "bool acquisition_timing_valid = false",
        "uint32_t timing_rejection_count = 0",
        "uint32_t recovery_attempt_count = 0",
        "uint32_t worker_stack_min_free_bytes = 0",
        "int8_t z_axis_sign = 0",
        "sampleDistanceGeometry(DistanceGeometrySnapshot *geometry)",
    ):
        require_contains(snapshot, fragment)

    for fragment in (
        "#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && "
        "CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART",
        'sample_imu("distance-geometry", false)',
        "geometry->imu = s_snapshot.imu",
        "geometry->sensor_fusion = s_snapshot.sensor_fusion",
        "sampled_at_us > 0 ? sampled_at_us : esp_timer_get_time()",
    ):
        require_contains(hardware, fragment)

    require_contains(wiring, "must never be connected together")
    require_contains(wiring, "one 500 ms")
    require_contains(wiring, "version-1 record is not migrated")
    require_contains(wiring, "four 500-sample windows live in PSRAM")
    require(
        "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y" in sdkconfig_defaults,
        "full profile must allow firmware BSS in external memory",
    )
    verify_tof_layout(gui_root)

    require(
        synchronized_acquisition(
            1_000_000,
            (1_010_000, 1_059_999),
            1_100_000,
            1_100_000,
        ),
        "valid acquisition at the TOF skew boundary was rejected",
    )
    require(
        not synchronized_acquisition(
            1_000_000,
            (1_010_000, 1_060_001),
            1_100_000,
            1_100_000,
        ),
        "TOF pair beyond the skew limit was accepted",
    )
    require(
        not synchronized_acquisition(
            1_000_000,
            (1_010_000, 1_020_000),
            1_500_001,
            1_500_001,
        ),
        "TOF-to-geometry skew beyond the limit was accepted",
    )
    require(
        [recovery_backoff_ms(value) for value in range(1, 8)]
        == [1_000, 2_000, 4_000, 8_000, 16_000, 30_000, 30_000],
        "adaptive recovery backoff does not match the firmware contract",
    )
    started_at_us = 1_000_000
    deadline_us = calibration_deadline_us(
        started_at_us,
        sample_count=500,
        interval_ms=250,
    )
    require(
        deadline_us - started_at_us == 280_000_000,
        "default calibration deadline lost its two-attempt budget or margin",
    )
    require(
        calibration_deadline_us(
            started_at_us,
            sample_count=500,
            interval_ms=1_000,
        )
        - started_at_us
        == 1_030_000_000,
        "maximum calibration deadline is not bounded as specified",
    )
    running_state, running_error = calibration_terminal_result(
        previous_record_valid=True,
        consecutive_transport_failures=2,
        transport_error="ESP_ERR_NOT_FOUND",
        now_us=deadline_us - 1,
        deadline_us=deadline_us,
    )
    require(
        (running_state, running_error) == ("running", None)
        and not settings_mutation_allowed(running_state),
        "calibration terminated before the transport threshold",
    )
    ready_state, ready_error = calibration_terminal_result(
        previous_record_valid=True,
        consecutive_transport_failures=3,
        transport_error="ESP_ERR_NOT_FOUND",
        now_us=deadline_us - 1,
        deadline_us=deadline_us,
    )
    require(
        (ready_state, ready_error) == ("ready", "ESP_ERR_NOT_FOUND")
        and settings_mutation_allowed(ready_state),
        "transport abort lost the real error or kept setters blocked",
    )
    failed_state, failed_error = calibration_terminal_result(
        previous_record_valid=False,
        consecutive_transport_failures=3,
        transport_error="ESP_ERR_INVALID_RESPONSE",
        now_us=deadline_us - 1,
        deadline_us=deadline_us,
    )
    require(
        (failed_state, failed_error)
        == ("failed", "ESP_ERR_INVALID_RESPONSE")
        and settings_mutation_allowed(failed_state),
        "transport abort without a prior record must enter Failed",
    )
    timeout_state, timeout_error = calibration_terminal_result(
        previous_record_valid=True,
        consecutive_transport_failures=0,
        transport_error="ESP_OK",
        now_us=deadline_us,
        deadline_us=deadline_us,
    )
    require(
        (timeout_state, timeout_error) == ("ready", "ESP_ERR_TIMEOUT")
        and settings_mutation_allowed(timeout_state),
        "wall-clock expiry must leave Running and unblock setters",
    )
    require(
        rate_limited_warning_emissions(
            [
                ("runtime-range", 1),
                ("runtime-range", 1_000_001),
                ("calibration-range", 1_000_001),
                ("runtime-range", 10_000_001),
                ("calibration-range", 10_000_001),
                ("calibration-range", 11_000_001),
            ]
        )
        == [
            ("runtime-range", 1, 0),
            ("calibration-range", 1_000_001, 0),
            ("runtime-range", 10_000_001, 1),
            ("calibration-range", 11_000_001, 1),
        ],
        "worker warning limiter is not independent per category or lost its suppressed count",
    )

    stable = [1000 + ((index % 7) - 3) for index in range(96)]
    contaminated = stable + [35, 3990, 2500, 70]
    mean_mm, mad_mm, inliers = robust_mean(contaminated)
    require(997 <= mean_mm <= 1003, f"outlier rejection shifted mean to {mean_mm}")
    require(mad_mm <= 3, f"unexpected MAD {mad_mm}")
    require(inliers == len(stable), f"expected {len(stable)} inliers, got {inliers}")

    reference_mm = 1500
    true_offset_mm = 17
    calibration_offsets = []
    for index in range(100):
        tilt_degrees = float(index % 6)
        axis_factor = cos(radians(tilt_degrees))
        expected_slant_mm = round(reference_mm / axis_factor)
        raw_mm = expected_slant_mm - true_offset_mm
        calibration_offsets.append(expected_slant_mm - raw_mm)
    offset_mean_mm, offset_mad_mm, offset_inliers = robust_mean(
        calibration_offsets
    )
    require(
        offset_mean_mm == true_offset_mm,
        "per-sample tilt normalization biased the calibration offset",
    )
    require(offset_mad_mm == 0, "stable normalized offsets gained dispersion")
    require(
        offset_inliers == len(calibration_offsets),
        "stable normalized offsets were rejected",
    )

    runtime_vertical = []
    for index in range(100):
        tilt_degrees = float(index % 16)
        axis_factor = cos(radians(tilt_degrees))
        raw_mm = round(reference_mm / axis_factor) - true_offset_mm
        runtime_vertical.append(round((raw_mm + true_offset_mm) * axis_factor))
    vertical_mean_mm, vertical_mad_mm, vertical_inliers = robust_mean(
        runtime_vertical
    )
    require(
        abs(vertical_mean_mm - reference_mm) <= 1,
        "per-sample runtime verticalization retained pose bias",
    )
    require(vertical_mad_mm <= 1, "runtime verticalization gained dispersion")
    require(
        vertical_inliers == len(runtime_vertical),
        "stable verticalized runtime samples were rejected",
    )

    print(
        "TOF distance verification: PASS "
        "(dual UART pins, 20..500 settings, MAD outlier rejection, "
        "strict pair/IMU timing, pair-synchronous calibration, "
        "adaptive recovery, PSRAM windows, stack telemetry, "
        "bounded 10 s warning rate limiting, "
        "bounded calibration transport/deadline termination, "
        "per-sample tilt-normalized runtime windows, "
        "320dp clipping-safe TOF layouts, "
        "default-off build-time gated driver)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
