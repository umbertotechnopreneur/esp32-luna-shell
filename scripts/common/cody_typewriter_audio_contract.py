"""Deferred source contracts for frame-driven original Cody typewriter audio."""
from pathlib import Path
import re


# source: Existing authored C++ source, read without importing firmware code.
# name: Exact function identity whose bounded body is required.
# Raises AssertionError if the function is missing or its body is incomplete.
def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
    assert match is not None, f"Missing typewriter function: {name}"
    start = match.end()
    depth = 1
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index]
    raise AssertionError(f"Unclosed typewriter function: {name}")


# root: Existing repository root; all observations remain read-only.
# Raises AssertionError for blocking render admission, a second transport owner,
# stale-scene playback, changed cue bounds, or bypassed canonical audio policy.
def check_typewriter_audio_source(root: Path) -> None:
    owner = (root / "components/LUNASHELL_platform/src/audio/LUNASHELL_audio.cpp").read_text(encoding="utf-8")
    cue = (root / "components/CODY_companion/src/scenes/CODY_typewriter_audio.cpp").read_text(encoding="utf-8")
    cmake = (root / "components/CODY_companion/CMakeLists.txt").read_text(encoding="utf-8")
    assert '"src/scenes/CODY_typewriter_audio.cpp"' in cmake
    assert "constexpr auto kOriginalTap = make_tap();" in cue
    sample_rate = int(re.search(r"kTapSampleRateHz\s*=\s*(\d+)", cue).group(1))
    sample_count = int(re.search(r"kTapSampleCount\s*=\s*(\d+)", cue).group(1))
    assert sample_rate == 16000 and 15 <= sample_count * 1000 / sample_rate <= 25
    assert sample_count * 2 <= 800
    assert "kOriginalTap.front() == 0 && kOriginalTap.back() == 0" in cue
    assert "_binary_" not in cue and "esp_codec_dev" not in cue and "i2s_" not in cue

    admission = function_body(owner, "LUNASHELL_audio_notify_typewriter")
    suspension = function_body(owner, "LUNASHELL_audio_set_typewriter_suspended")
    forbidden = r"\b(xSemaphoreTake|vTaskDelay|xTaskNotifyWait|malloc|calloc|realloc|esp_codec_dev_write)\s*\(|\bnew\s"
    assert re.search(forbidden, admission) is None
    assert re.search(forbidden, suspension) is None
    claim = function_body(owner, "claim_typewriter_event")
    invalidation = function_body(owner, "invalidate_typewriter_events")
    dropping = function_body(owner, "drop_typewriter_event")
    epoch_check = function_body(owner, "typewriter_event_current")
    assert "portMUX_TYPE s_typewriter_mailbox_mux = portMUX_INITIALIZER_UNLOCKED" in owner
    assert "eSetBits" in admission and "s_typewriter_mailbox.pending = true" in admission
    assert "s_typewriter_mailbox.latest = event" in admission
    assert "event = {requested_at_us, s_typewriter_mailbox.generation, s_typewriter_mailbox.sequence}" in admission
    assert "++s_typewriter_mailbox.generation" in suspension
    assert "s_typewriter_mailbox.pending = false" in suspension
    assert "event = s_typewriter_mailbox.latest" in claim and "s_typewriter_mailbox.pending = false" in claim
    assert claim.index("event = s_typewriter_mailbox.latest") < claim.index("s_typewriter_mailbox.pending = false")
    assert "latest.sequence == event.sequence" in dropping
    assert "generation == s_typewriter_mailbox.generation" in epoch_check
    assert "s_typewriter_pending" not in owner and "s_typewriter_requested_generation" not in owner
    # Every shared payload mutation is bounded; scheduler, logging and I/O stay outside.
    critical_io = r"\b(xTaskNotify|xSemaphoreTake|vTaskDelay|esp_timer_get_time|esp_codec_dev_\w+|ESP_LOG\w+|reportPlayback)\s*\("
    for mailbox_operation in (admission, suspension, claim, invalidation, dropping, epoch_check):
        enter = "portENTER_CRITICAL(&s_typewriter_mailbox_mux);"
        leave = "portEXIT_CRITICAL(&s_typewriter_mailbox_mux);"
        assert mailbox_operation.count(enter) == 1 and mailbox_operation.count(leave) == 1
        critical = mailbox_operation.split(enter, 1)[1].split(leave, 1)[0]
        assert re.search(critical_io, critical) is None

    preparation = function_body(owner, "LUNASHELL_audio_prepare_typewriter")
    assert "xTaskCreateStatic(" in preparation
    assert "LUNASHELL_audio_init(" not in preparation and "esp_codec_dev_write(" not in preparation
    assert "sample_count < 240U || sample_count > 400U" in preparation
    assert "TYPEWRITER_INTERVAL_US = 50000" in owner
    assert "TYPEWRITER_MAX_AGE_US = 150000" in owner

    worker = function_body(owner, "typewriter_worker")
    playback = function_body(owner, "play_typewriter_tap_locked")
    shared_playback = function_body(owner, "LUNASHELL_audio_play_pcm16")
    assert "clock_cue_policy(clock)" in worker
    assert "ClockCuePolicy::Allowed" in worker and "readClockObservation(clock)" in worker
    assert "xSemaphoreTake(mutex, 0)" in worker and "TYPEWRITER_MAX_AGE_US" in worker
    assert "claim_typewriter_event(event)" in worker
    assert "observed_us - event.requested_at_us" in worker and "event.generation" in worker
    assert "typewriter_event_current(generation)" in playback and "s_standby.load" in playback
    assert "esp_codec_dev_get_out_vol" in playback and "volume_percent == 0" in playback
    assert "esp_codec_dev_set_out_vol" not in playback
    assert "write_speaker_pcm_locked(" in playback and "write_speaker_pcm_locked(" in shared_playback
    assert "esp_codec_dev_write(" not in playback
    assert "s_typewriter_last_error.load" in admission
    retained_fault = function_body(owner, "retain_typewriter_failure")
    assert "s_typewriter_last_error.exchange" in retained_fault and "ESP_LOGE" in retained_fault
    # The canonical failure publication must follow, not live inside, log coalescing.
    assert re.search(r"\}\s*publish_rejected_playback\(error, sample_rate_hz, volume_percent\);", retained_fault)
    publication = function_body(owner, "publish_rejected_playback")
    assert "reportPlayback(" in publication and "false, 0, sample_rate_hz, volume_percent, error" in publication
