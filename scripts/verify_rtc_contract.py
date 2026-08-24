"""Deterministic checks for RTC reset-state and real-source recovery."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HARDWARE = ROOT / "components" / "LUNASHELL_platform" / "LUNASHELL_hardware.cpp"
TIME = ROOT / "components" / "LUNASHELL_platform" / "LUNASHELL_time.cpp"
WIFI = ROOT / "components" / "LUNASHELL_platform" / "LUNASHELL_wifi.cpp"

ESP_OK = 0
ESP_ERR_INVALID_STATE = 1
ESP_ERR_INVALID_RESPONSE = 2
ESP_ERR_PUBLICATION_FAILED = 3


@dataclass(frozen=True)
class Calendar:
    integrity: bool
    year: int
    month: int
    day: int
    hour: int
    minute: int
    second: int


@dataclass
class RtcLatchModel:
    """Executable specification for the persistent PCF85063 OS condition."""

    oscillator_stop_latched: bool = False
    recovery_readback_required: bool = False
    trusted_write_generation: int = 0
    last_attempt_at_us: int = 0
    last_error: int = ESP_OK
    physical_read_count: int = 0

    def complete_physical_read(
        self,
        *,
        oscillator_stop: bool,
        read_error: int,
        publication_ok: bool,
        generation_at_read: int,
    ) -> None:
        if not publication_ok:
            return
        if read_error == ESP_OK:
            return
        if read_error != ESP_ERR_INVALID_STATE or not oscillator_stop:
            return

        if self.trusted_write_generation != generation_at_read:
            return
        self.oscillator_stop_latched = True

    def periodic_read(
        self,
        attempted_at_us: int,
        *,
        oscillator_stop: bool,
        publication_ok: bool = True,
        commit_ok: bool = True,
    ) -> int:
        if self.oscillator_stop_latched:
            return ESP_ERR_INVALID_STATE

        generation_at_read = self.trusted_write_generation
        self.physical_read_count += 1
        read_error = (
            ESP_ERR_INVALID_STATE if oscillator_stop else ESP_OK
        )
        if publication_ok and oscillator_stop:
            self.recovery_readback_required = True
        if publication_ok and commit_ok:
            self.last_attempt_at_us = attempted_at_us
            self.last_error = read_error
            self.complete_physical_read(
                oscillator_stop=oscillator_stop,
                read_error=read_error,
                publication_ok=True,
                generation_at_read=generation_at_read,
            )
        if read_error != ESP_OK:
            return read_error
        return ESP_OK if publication_ok else ESP_ERR_PUBLICATION_FAILED

    def trusted_write(
        self,
        requested: Calendar,
        *,
        observed: Calendar | None = None,
        write_ok: bool = True,
        readback_ok: bool = True,
        publication_ok: bool = True,
    ) -> int:
        recovery_readback_required = (
            self.oscillator_stop_latched or
            self.recovery_readback_required
        )
        self.recovery_readback_required = False
        if not write_ok:
            if recovery_readback_required:
                self.recovery_readback_required = True
            return ESP_ERR_INVALID_RESPONSE

        error = ESP_OK
        if recovery_readback_required:
            self.physical_read_count += 1
            if not readback_ok or observed is None:
                error = ESP_ERR_INVALID_RESPONSE
            elif not rtc_write_readback_matches(requested, observed):
                error = ESP_ERR_INVALID_RESPONSE

        if error == ESP_OK and publication_ok:
            self.trusted_write_generation += 1
            self.oscillator_stop_latched = False
            return ESP_OK
        if recovery_readback_required:
            self.recovery_readback_required = True
        if error != ESP_OK:
            return error
        return ESP_ERR_PUBLICATION_FAILED


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_contains(text: str, fragment: str, source: str) -> None:
    require(fragment in text, f"{fragment!r} missing from {source}")


def bcd(value: int) -> int:
    return ((value >> 4) * 10) + (value & 0x0F)


def decode(raw: tuple[int, ...]) -> Calendar:
    require(len(raw) == 7, "RTC register image must contain seven bytes")
    return Calendar(
        integrity=(raw[0] & 0x80) == 0,
        year=2000 + bcd(raw[6]),
        month=bcd(raw[5] & 0x1F),
        day=bcd(raw[3] & 0x3F),
        hour=bcd(raw[2] & 0x3F),
        minute=bcd(raw[1] & 0x7F),
        second=bcd(raw[0] & 0x7F),
    )


def calendar_datetime(value: Calendar) -> datetime:
    return datetime(
        value.year,
        value.month,
        value.day,
        value.hour,
        value.minute,
        value.second,
    )


def rtc_write_readback_matches(
    requested: Calendar,
    observed: Calendar,
) -> bool:
    requested_at = calendar_datetime(requested)
    next_at = requested_at + timedelta(seconds=1)
    observed_at = calendar_datetime(observed)
    return observed_at in (requested_at, next_at)


def gps_sync_decision(
    *,
    trusted_fix_observed: bool,
    system_time_valid: bool,
    drift_us: int,
    last_persisted_epoch: int,
    gps_epoch: int,
) -> tuple[bool, bool]:
    """Executable specification for coalesced GNSS clock persistence."""

    system_sync_required = (
        not trusted_fix_observed or
        not system_time_valid or
        abs(drift_us) >= 2_000_000
    )
    persisted_delta_seconds = (
        6 * 60 * 60
        if last_persisted_epoch == 0
        else abs(gps_epoch - last_persisted_epoch)
    )
    rtc_persistence_required = (
        system_sync_required or
        persisted_delta_seconds >= 6 * 60 * 60
    )
    return system_sync_required, rtc_persistence_required


def rtc_retry_delay_seconds(previous_delay_seconds: int) -> int:
    """Executable specification for bounded RTC persistence backoff."""

    if previous_delay_seconds == 0:
        return 30
    if previous_delay_seconds <= (30 * 60) // 2:
        return previous_delay_seconds * 2
    return 30 * 60


def rtc_retry_decision(
    *,
    now_us: int,
    retry_after_us: int,
    retained_error: int,
) -> tuple[bool, int]:
    if retry_after_us != 0 and now_us < retry_after_us:
        return False, retained_error
    return True, ESP_OK


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


def verify_latch_model() -> None:
    reset = Calendar(False, 2000, 1, 1, 0, 0, 0)
    model = RtcLatchModel()
    require(
        model.periodic_read(100, oscillator_stop=True) ==
        ESP_ERR_INVALID_STATE,
        "first OS sample must preserve ESP_ERR_INVALID_STATE",
    )
    require(model.oscillator_stop_latched, "OS sample did not latch")
    require(model.physical_read_count == 1, "first OS sample was not read")
    require(model.last_attempt_at_us == 100, "first OS attempt was not recorded")
    require(model.last_error == ESP_ERR_INVALID_STATE, "OS error was lost")
    require(
        model.recovery_readback_required,
        "OS sample did not require a recovery readback",
    )

    require(
        model.periodic_read(200, oscillator_stop=True) ==
        ESP_ERR_INVALID_STATE,
        "latched periodic sample must keep ESP_ERR_INVALID_STATE",
    )
    require(
        model.physical_read_count == 1,
        "latched periodic sample repeated the physical read",
    )
    require(
        model.last_attempt_at_us == 100,
        "latched periodic sample manufactured a new attempt timestamp",
    )
    model.complete_physical_read(
        oscillator_stop=False,
        read_error=ESP_OK,
        publication_ok=True,
        generation_at_read=model.trusted_write_generation,
    )
    require(
        model.oscillator_stop_latched,
        "a successful read cleared the latch without a trusted write",
    )

    same_refresh = RtcLatchModel()
    require(
        same_refresh.periodic_read(
            300,
            oscillator_stop=True,
            commit_ok=False,
        ) == ESP_ERR_INVALID_STATE,
        "staged OS sample did not preserve its read error",
    )
    require(
        not same_refresh.oscillator_stop_latched,
        "failed snapshot commit applied the persistent OS latch",
    )
    require(
        same_refresh.recovery_readback_required,
        "same-refresh trusted write could miss the staged OS observation",
    )
    require(
        same_refresh.trusted_write(
            Calendar(True, 2026, 12, 31, 23, 59, 59),
            observed=Calendar(True, 2027, 1, 1, 0, 0, 0),
        ) == ESP_OK,
        "same-refresh trusted write did not verify staged OS recovery",
    )
    require(
        same_refresh.physical_read_count == 2,
        "same-refresh OS recovery skipped its hardware readback",
    )

    requested = Calendar(True, 2026, 12, 31, 23, 59, 59)
    next_second = Calendar(True, 2027, 1, 1, 0, 0, 0)
    mismatched = Calendar(True, 2027, 1, 1, 0, 0, 1)
    require(
        rtc_write_readback_matches(requested, requested),
        "same-second RTC recovery readback was rejected",
    )
    require(
        rtc_write_readback_matches(requested, next_second),
        "next-second RTC recovery readback was rejected",
    )
    require(
        not rtc_write_readback_matches(requested, mismatched),
        "RTC recovery accepted a readback beyond the one-second window",
    )

    require(
        model.trusted_write(
            requested,
            observed=next_second,
            publication_ok=False,
        ) == ESP_ERR_PUBLICATION_FAILED,
        "failed recovery publication did not remain visible",
    )
    require(
        model.oscillator_stop_latched,
        "failed recovery publication cleared the OS latch",
    )
    require(
        model.trusted_write(requested, observed=mismatched) ==
        ESP_ERR_INVALID_RESPONSE,
        "mismatched recovery readback did not fail",
    )
    require(
        model.oscillator_stop_latched,
        "failed recovery readback cleared the OS latch",
    )
    require(
        model.trusted_write(requested, observed=next_second) == ESP_OK,
        "verified recovery write did not succeed",
    )
    require(
        not model.oscillator_stop_latched,
        "verified recovery write did not clear the OS latch",
    )

    reads_before_normal_write = model.physical_read_count
    require(
        model.trusted_write(requested) == ESP_OK,
        "normal trusted write failed",
    )
    require(
        model.physical_read_count == reads_before_normal_write,
        "normal trusted write performed the recovery-only readback",
    )

    stale_generation = model.trusted_write_generation
    require(model.trusted_write(requested) == ESP_OK, "race setup write failed")
    model.complete_physical_read(
        oscillator_stop=True,
        read_error=ESP_ERR_INVALID_STATE,
        publication_ok=True,
        generation_at_read=stale_generation,
    )
    require(
        not model.oscillator_stop_latched,
        "a stale OS read re-latched after a newer trusted write",
    )

    newer_generation = model.trusted_write_generation
    model.complete_physical_read(
        oscillator_stop=True,
        read_error=ESP_ERR_INVALID_STATE,
        publication_ok=True,
        generation_at_read=newer_generation,
    )
    require(
        model.oscillator_stop_latched,
        "current-generation OS observation did not latch",
    )
    model.complete_physical_read(
        oscillator_stop=True,
        read_error=ESP_ERR_INVALID_STATE,
        publication_ok=True,
        generation_at_read=stale_generation,
    )
    require(
        model.oscillator_stop_latched,
        "a stale OS observation cleared a newer RTC latch",
    )

    require(not reset.integrity, "model reset fixture must carry OS state")


def verify_gps_sync_model() -> None:
    first = gps_sync_decision(
        trusted_fix_observed=False,
        system_time_valid=True,
        drift_us=0,
        last_persisted_epoch=0,
        gps_epoch=1_800_000_000,
    )
    require(first == (True, True), "first trusted GPS fix must fully synchronize")

    steady = gps_sync_decision(
        trusted_fix_observed=True,
        system_time_valid=True,
        drift_us=250_000,
        last_persisted_epoch=1_800_000_000,
        gps_epoch=1_800_000_001,
    )
    require(steady == (False, False), "steady GPS fixes must be coalesced")

    drifted = gps_sync_decision(
        trusted_fix_observed=True,
        system_time_valid=True,
        drift_us=2_000_000,
        last_persisted_epoch=1_800_000_000,
        gps_epoch=1_800_000_002,
    )
    require(drifted == (True, True), "clock drift must synchronize and persist")

    interval = gps_sync_decision(
        trusted_fix_observed=True,
        system_time_valid=True,
        drift_us=0,
        last_persisted_epoch=1_800_000_000,
        gps_epoch=1_800_000_000 + 6 * 60 * 60,
    )
    require(interval == (False, True), "RTC maximum persistence interval was lost")

    delay = 0
    for expected in (30, 60, 120, 240, 480, 960, 1800, 1800):
        delay = rtc_retry_delay_seconds(delay)
        require(delay == expected, "RTC retry backoff is not bounded exponential")
    require(
        rtc_retry_decision(
            now_us=1_000_000,
            retry_after_us=2_000_000,
            retained_error=ESP_ERR_INVALID_RESPONSE,
        ) == (False, ESP_ERR_INVALID_RESPONSE),
        "RTC backoff must retain the original persistence error",
    )
    require(
        rtc_retry_decision(
            now_us=2_000_000,
            retry_after_us=2_000_000,
            retained_error=ESP_ERR_INVALID_RESPONSE,
        ) == (True, ESP_OK),
        "RTC persistence retry did not become due at its deadline",
    )


def main() -> int:
    hardware = HARDWARE.read_text(encoding="utf-8")
    time_source = TIME.read_text(encoding="utf-8")
    wifi = WIFI.read_text(encoding="utf-8")

    reset_calendar = decode((0x80, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00))
    require(not reset_calendar.integrity, "power-on OS flag must invalidate integrity")
    require(reset_calendar.year == 2000, "power-on year must decode structurally")

    read_start = hardware.index("esp_err_t read_rtc_calendar(")
    read_end = hardware.index("esp_err_t publish_rtc_read(", read_start)
    rtc_read = hardware[read_start:read_end]
    for fragment in (
        "valid_rtc_register_calendar(decoded)",
        "*time = decoded;",
        "if (!decoded.clock_integrity)",
        "if (!valid_calendar_time(decoded))",
        "return ESP_ERR_INVALID_STATE;",
    ):
        require_contains(rtc_read, fragment, "read_rtc_calendar")
    require(
        rtc_read.index("valid_rtc_register_calendar(decoded)")
        < rtc_read.index("*time = decoded;")
        < rtc_read.index("if (!decoded.clock_integrity)")
        < rtc_read.index("if (!valid_calendar_time(decoded))"),
        "RTC read must separate malformed registers, OS integrity, and trusted epoch",
    )
    publish_start = hardware.index("esp_err_t publish_rtc_read(")
    publish_end = hardware.index("esp_err_t sample_rtc(", publish_start)
    rtc_publish = hardware[publish_start:publish_end]
    require_contains(
        rtc_publish,
        "error == ESP_ERR_INVALID_STATE &&",
        "RTC invalid-state publication",
    )
    require_contains(
        rtc_publish,
        "valid_rtc_register_calendar(time)",
        "RTC invalid-state publication",
    )
    require_contains(
        rtc_publish,
        "snapshot.rtc.year = time.year;",
        "RTC recovery-state publication",
    )
    for fragment in (
        "s_rtc_persistence_error_latched.load(",
        "s_rtc_persistence_failure_at_us.load(",
        "if (persistence_error != ESP_OK)",
        "mark_sample_failure(",
        "persistence_failure_at_us",
    ):
        require_contains(
            rtc_publish,
            fragment,
            "latched RTC persistence failure publication",
        )
    rtc_success_copy_at = rtc_publish.find("snapshot.rtc = sample;")
    rtc_persistence_guard_at = rtc_publish.find(
        "if (persistence_error != ESP_OK)",
        rtc_success_copy_at,
    )
    require(
        0 <= rtc_success_copy_at < rtc_persistence_guard_at,
        "a successful RTC read must restore a pending write failure after "
        "copying current calendar data",
    )
    require_contains(
        hardware,
        "RTC PCF85063: detected, 24-hour mode configured",
        "RTC initialization log",
    )
    require(
        "oscillator running in 24-hour mode" not in hardware,
        "RTC initialization must not claim oscillator integrity before reading OS",
    )

    sample = function_text(hardware, "esp_err_t sample_rtc(", "sample_rtc")
    skip_at = sample.index("s_rtc_oscillator_stop_latched.load(")
    invalid_at = sample.index("return ESP_ERR_INVALID_STATE;", skip_at)
    attempt_at = sample.index("const int64_t attempted_at_us", invalid_at)
    physical_read_at = sample.index("read_rtc_calendar(&time)", attempt_at)
    publication_at = sample.index("publish_rtc_read(time, error, attempted_at_us)")
    require(
        skip_at < invalid_at < attempt_at < physical_read_at < publication_at,
        "periodic OS latch must return before timestamping, I2C, or publication",
    )
    require_contains(
        sample[:invalid_at],
        "high_frequency_sample_phase(phase)",
        "periodic-only RTC OS latch",
    )

    latch_observation = function_text(
        hardware,
        "RtcLatchObservation make_rtc_latch_observation(",
        "RTC OS latch observation",
    )
    for fragment in (
        "if (publication_error != ESP_OK)",
        "read_error != ESP_ERR_INVALID_STATE",
        "valid_rtc_register_calendar(time)",
        "observation.valid = true",
        "observation.oscillator_stop = true",
        "observation.trusted_write_generation_at_read =",
    ):
        require_contains(
            latch_observation,
            fragment,
            "RTC OS latch observation",
        )

    latch_apply_locked = function_text(
        hardware,
        "void apply_rtc_latch_observation_locked(",
        "locked RTC OS latch application",
    )
    for fragment in (
        "if (!observation.valid)",
        "if (!observation.oscillator_stop)",
        "s_rtc_trusted_write_generation.load(std::memory_order_acquire) !=",
        "observation.trusted_write_generation_at_read",
        "return;",
        "s_rtc_oscillator_stop_latched.store(true",
    ):
        require_contains(
            latch_apply_locked,
            fragment,
            "locked RTC OS latch application",
        )
    require(
        latch_apply_locked.count("s_rtc_oscillator_stop_latched.store(") == 1,
        "an RTC observation may only set its own current-generation latch",
    )
    require(
        latch_apply_locked.index("s_rtc_trusted_write_generation.load(") <
        latch_apply_locked.index("s_rtc_oscillator_stop_latched.store(true"),
        "generation ownership must be verified before setting the RTC latch",
    )

    latch_apply = function_text(
        hardware,
        "void apply_rtc_latch_observation(",
        "RTC OS latch application",
    )
    for fragment in (
        "std::lock_guard<std::mutex> rtc_lock(s_rtc_operation_mutex);",
        "apply_rtc_latch_observation_locked(observation);",
    ):
        require_contains(latch_apply, fragment, "RTC OS latch application")

    refresh = function_text(
        hardware,
        "esp_err_t DeviceSnapshotService::refresh(",
        "DeviceSnapshotService::refresh",
    )
    commit_at = refresh.index("const esp_err_t commit_error = commit_snapshot_stage(")
    committed_at = refresh.index("summary_available = commit_error == ESP_OK", commit_at)
    apply_at = refresh.index(
        "apply_rtc_latch_observation(rtc_latch_observation)",
        committed_at,
    )
    require(
        commit_at < committed_at < apply_at,
        "periodic RTC latch observation must apply only after snapshot commit",
    )

    readback_match = function_text(
        hardware,
        "bool rtc_write_readback_matches(",
        "RTC write readback matching",
    )
    for fragment in (
        "calendar_fields_equal(requested, observed)",
        "advance_calendar_one_second(&next)",
        "calendar_fields_equal(next, observed)",
    ):
        require_contains(readback_match, fragment, "RTC write readback matching")
    recovery_requirement = function_text(
        hardware,
        "void require_rtc_recovery_readback(",
        "same-refresh RTC recovery requirement",
    )
    for fragment in (
        "observation.valid && observation.oscillator_stop",
        "s_rtc_recovery_readback_required.store(",
        "true",
        "std::memory_order_release",
    ):
        require_contains(
            recovery_requirement,
            fragment,
            "same-refresh RTC recovery requirement",
        )
    require_contains(
        sample,
        "require_rtc_recovery_readback(observation);",
        "periodic RTC recovery requirement",
    )
    require_contains(
        sample,
        "std::lock_guard<std::mutex> rtc_lock(s_rtc_operation_mutex);",
        "serialized periodic RTC read",
    )
    require(
        sample.count("s_rtc_oscillator_stop_latched.load(") == 2,
        "periodic RTC latch must be rechecked after acquiring its operation lock",
    )

    rtc_write = function_text(
        hardware,
        'extern "C" esp_err_t LUNASHELL_rtc_write(',
        "LUNASHELL_rtc_write",
    )
    for fragment in (
        "bool recovery_readback_required =",
        "s_rtc_recovery_readback_required.exchange(",
        "if (recovery_readback_required)",
        "error = read_rtc_calendar(&published);",
        "rtc_write_readback_matches(*time, published)",
        "if (error == ESP_OK && publication_error == ESP_OK)",
        "s_rtc_trusted_write_generation.fetch_add(",
        "s_rtc_oscillator_stop_latched.store(",
        "(error != ESP_OK || publication_error != ESP_OK)",
        "s_rtc_recovery_readback_required.store(",
    ):
        require_contains(rtc_write, fragment, "conditional RTC recovery write")
    require(
        rtc_write.count("s_rtc_recovery_readback_required.exchange(") == 2,
        "RTC write must consume recovery requests before and after its write",
    )
    require_contains(
        rtc_write,
        "std::lock_guard<std::mutex> rtc_lock(s_rtc_operation_mutex);",
        "serialized trusted RTC write/readback",
    )
    direct_read = function_text(
        hardware,
        'extern "C" esp_err_t LUNASHELL_rtc_read(',
        "LUNASHELL_rtc_read",
    )
    require_contains(
        direct_read,
        "std::lock_guard<std::mutex> rtc_lock(s_rtc_operation_mutex);",
        "serialized direct RTC read",
    )
    require_contains(
        direct_read,
        "apply_rtc_latch_observation_locked(observation);",
        "direct RTC latch application under the existing operation lock",
    )
    recovery_at = rtc_write.index("if (recovery_readback_required)")
    readback_at = rtc_write.index("read_rtc_calendar(&published)", recovery_at)
    publish_at = rtc_write.index("publish_rtc_read(published, error", readback_at)
    success_at = rtc_write.index(
        "if (error == ESP_OK && publication_error == ESP_OK)",
        publish_at,
    )
    generation_at = rtc_write.index(
        "s_rtc_trusted_write_generation.fetch_add(",
        success_at,
    )
    clear_at = rtc_write.index(
        "s_rtc_oscillator_stop_latched.store(",
        generation_at,
    )
    require(
        recovery_at < readback_at < publish_at < success_at < generation_at < clear_at,
        "RTC recovery must verify and publish before advancing generation or clearing",
    )
    for fragment in (
        "s_rtc_persistence_error_latched.store(",
        "s_rtc_persistence_failure_at_us.store(",
        "const esp_err_t result = sample_publication_result(",
        "if (result != ESP_OK)",
    ):
        require_contains(
            rtc_write,
            fragment,
            "RTC persistence failure latch",
        )

    for fragment in (
        "config.sync_cb = sntp_sync_callback;",
        "persist_epoch_to_rtc(time_value->tv_sec)",
        "kGpsSystemResyncDriftUs",
        "kGpsRtcPersistenceIntervalSeconds",
        "kRtcPersistenceRetryInitialSeconds",
        "kRtcPersistenceRetryMaximumSeconds",
    ):
        require_contains(time_source, fragment, "real UTC synchronization")
    sntp_callback = function_text(
        time_source,
        "void sntp_sync_callback(",
        "SNTP callback",
    )
    require_contains(
        sntp_callback,
        'report_time(true, true, "sntp", ESP_OK);',
        "SNTP system-time publication",
    )
    sntp_lock_at = sntp_callback.find(
        "std::lock_guard<std::mutex> sync_lock(s_sync_mutex)"
    )
    sntp_validation_at = sntp_callback.find(
        "if (time_value == nullptr || !epoch_valid(time_value->tv_sec))"
    )
    require(
        0 <= sntp_lock_at < sntp_validation_at,
        "SNTP success and invalid-sample publications must share the UTC sync "
        "transaction",
    )
    sntp_invalid_end = sntp_callback.find(
        "const esp_err_t rtc_result",
        sntp_validation_at,
    )
    sntp_invalid_path = sntp_callback[sntp_validation_at:sntp_invalid_end]
    require_contains(
        sntp_invalid_path,
        "report_time_failure_preserving_valid_clock(ESP_ERR_INVALID_RESPONSE)",
        "invalid SNTP publication",
    )
    require(
        'report_time(false, false, "none", ESP_ERR_INVALID_RESPONSE)' not in
        sntp_invalid_path,
        "an invalid SNTP sample must not blindly invalidate a trusted clock",
    )
    preserve_clock = function_text(
        time_source,
        "void report_time_failure_preserving_valid_clock(",
        "SNTP clock preservation",
    )
    for fragment in (
        "s_reported_system_time_valid &&",
        "gettimeofday(&current_time, nullptr) == 0",
        "epoch_valid(current_time.tv_sec)",
        "s_reported_system_time_source",
        "error",
    ):
        require_contains(
            preserve_clock,
            fragment,
            "truthful invalid SNTP publication",
        )
    time_init = function_text(
        time_source,
        'extern "C" esp_err_t LUNASHELL_time_init(',
        "time initialization",
    )
    time_notify = function_text(
        time_source,
        'extern "C" esp_err_t LUNASHELL_time_notify_wifi_online(',
        "Wi-Fi-to-SNTP notification",
    )
    for body, first_shared_state, source in (
        (time_init, "if (s_initialized)", "time initialization"),
        (time_notify, "if (!s_initialized)", "SNTP start notification"),
    ):
        lock_at = body.find(
            "std::lock_guard<std::mutex> sync_lock(s_sync_mutex)"
        )
        shared_state_at = body.find(first_shared_state)
        require(
            0 <= lock_at < shared_state_at,
            f"{source} must serialize time-service state before its first read",
        )
    sntp_deferred_at = time_init.find("config.start = false;")
    sntp_init_at = time_init.find("esp_netif_sntp_init(&config)")
    require(
        0 <= sntp_deferred_at < sntp_init_at,
        "time initialization must keep SNTP callbacks deferred while holding "
        "the service-state transaction",
    )
    notify_lock_at = time_notify.find(
        "std::lock_guard<std::mutex> sync_lock(s_sync_mutex)"
    )
    sntp_start_at = time_notify.find("esp_netif_sntp_start()")
    require(
        0 <= notify_lock_at < sntp_start_at,
        "SNTP start must run inside the serialized time-service transaction",
    )
    gps_sync = function_text(
        time_source,
        'extern "C" esp_err_t LUNASHELL_time_sync_from_gps(',
        "GPS synchronization",
    )
    for fragment in (
        "system_sync_required",
        "rtc_persistence_required",
        'apply_system_utc(gps_epoch, "gps")',
        'report_time(true, true, "gps", ESP_OK);',
        "persist_epoch_to_rtc(gps_epoch)",
        "s_initialization_result = rtc_result;",
        "record_rtc_persistence_failure(",
        "record_rtc_persistence_success(gps_epoch);",
        "return s_rtc_persistence_error;",
    ):
        require_contains(gps_sync, fragment, "coalesced GPS synchronization")
    gps_sync_gate_at = gps_sync.find("if (system_sync_required)")
    gps_snapshot_repair_at = gps_sync.find(
        'report_time(true, true, "gps", ESP_OK);',
        gps_sync_gate_at,
    )
    gps_rtc_gate_at = gps_sync.find(
        "if (!rtc_persistence_required)",
        gps_snapshot_repair_at,
    )
    require(
        0 <= gps_sync_gate_at < gps_snapshot_repair_at < gps_rtc_gate_at,
        "a coalesced GPS fix must repair the system-time snapshot before the "
        "RTC persistence fast path returns",
    )
    gps_retry_gate_at = gps_sync.find(
        "if (s_rtc_persistence_retry_after_us != 0)",
        gps_rtc_gate_at,
    )
    gps_persist_at = gps_sync.find(
        "persist_epoch_to_rtc(gps_epoch)",
        gps_retry_gate_at,
    )
    require(
        0 <= gps_rtc_gate_at < gps_retry_gate_at < gps_persist_at,
        "RTC persistence backoff must gate physical writes after the cadence gate",
    )
    require_contains(
        wifi,
        "LUNASHELL_time_notify_wifi_online();",
        "DHCP-to-SNTP wiring",
    )
    require_contains(
        hardware,
        "LUNASHELL_time_sync_from_gps(year, month, day, hour, minute, second);",
        "GPS-to-RTC wiring",
    )

    verify_latch_model()
    verify_gps_sync_model()

    print(
        "RTC contract verification: PASS "
        "(OS latch, conditional recovery readback, generation race, "
        "trusted epoch, truthful SNTP state, retained persistence errors, "
        "coalesced GPS persistence, bounded retry)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
