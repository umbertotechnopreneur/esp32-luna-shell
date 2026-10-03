"""Static allocation and behavior contracts for the FFT UI binding cache."""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from common.source_contracts import read_component_source


ROOT = Path(__file__).resolve().parents[2]
APP_SOURCE = ROOT / "components" / "LUNASHELL_app" / "LUNASHELL_app.cpp"
APP_HEADER = (
    ROOT
    / "components"
    / "LUNASHELL_app"
    / "include"
    / "LUNASHELL_app.hpp"
)
RUNTIME_SOURCE = (
    ROOT
    / "third_party"
    / "esp-brookesia"
    / "gui"
    / "brookesia_gui_interface"
    / "src"
    / "runtime_binding.cpp"
)

MIC0_COLORS = (
    "#078dff", "#0b9dff", "#10afff", "#12c5ff",
    "#16d9ff", "#28e4f2", "#45d9ee", "#5abcfb",
    "#709dff", "#8780ff", "#a466ff", "#bd51ff",
    "#d342ff", "#e834f8", "#f62de7", "#ff35cf",
)
MIC1_COLORS = (
    "#ff8700", "#ff9500", "#ffa400", "#ffb300",
    "#ffc000", "#ffcb19", "#ffd43b", "#f4dd4b",
    "#e6e45a", "#d4e660", "#bfe365", "#a8df6b",
    "#8fdb77", "#72d98b", "#4dd6a3", "#25d3bb",
)


# path: Requested source; split implementation families are read together.
# Raises OSError or UnicodeError when a required source is unavailable.
def read(path: Path) -> str:
    return read_component_source(path)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_contains(text: str, fragment: str, source: str) -> None:
    require(fragment in text, f"{fragment!r} missing from {source}")


def forbid_contains(text: str, fragment: str, source: str) -> None:
    require(fragment not in text, f"{fragment!r} forbidden in {source}")


def compact_cpp(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def braced_text(text: str, marker: str, source: str) -> str:
    marker_at = text.find(marker)
    require(marker_at >= 0, f"{marker!r} missing from {source}")
    body_at = text.find("{", marker_at + len(marker))
    require(body_at >= 0, f"body for {marker!r} missing from {source}")

    depth = 0
    quote = ""
    escaped = False
    index = body_at
    while index < len(text):
        token = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if quote:
            if escaped:
                escaped = False
            elif token == "\\":
                escaped = True
            elif token == quote:
                quote = ""
        elif token in {'"', "'"}:
            quote = token
        elif token == "/" and following == "/":
            newline = text.find("\n", index + 2)
            index = len(text) if newline < 0 else newline
        elif token == "/" and following == "*":
            end_comment = text.find("*/", index + 2)
            require(end_comment >= 0, f"unterminated comment in {source}")
            index = end_comment + 1
        elif token == "{":
            depth += 1
        elif token == "}":
            depth -= 1
            if depth == 0:
                return text[marker_at : index + 1]
        index += 1
    raise AssertionError(f"unterminated body for {marker!r} in {source}")


def initializer_colors(source: str, name: str) -> tuple[str, ...]:
    match = re.search(
        rf"\b{name}\s*=\s*\{{(?P<body>.*?)\}};",
        source,
        flags=re.DOTALL,
    )
    require(match is not None, f"missing palette {name}")
    return tuple(re.findall(r'"(#[0-9a-fA-F]{6})"', match.group("body")))


def verify_cache_layout(source: str, header: str) -> None:
    cache = braced_text(
        source,
        "struct AudioFftBindingCache",
        "LUNASHELL_app.cpp",
    )
    expected_vectors = (
        "fft_hidden_updates",
        "fft_visible_updates",
        "detail_hidden_updates",
        "detail_palette_updates",
        "detail_visible_updates",
    )
    for name in expected_vectors:
        require_contains(cache, name, "AudioFftBindingCache")

    for fragment in (
        "struct AudioFftBindingCache;",
        "std::unique_ptr<app_detail::AudioFftBindingCache>",
        "audio_fft_binding_cache_;",
    ):
        require_contains(header, fragment, "LUNASHELL_app.hpp")

    factory = braced_text(
        source,
        "make_audio_fft_binding_cache()",
        "LUNASHELL_app.cpp",
    )
    reserve_contracts = (
        "fft_hidden_updates.reserve(1U)",
        "fft_visible_updates.reserve(kAudioFftVisibleBindingCount)",
        "detail_hidden_updates.reserve(2U)",
        "detail_palette_updates.reserve(kAudioDetailBandBindingCount)",
        "detail_visible_updates.reserve(kAudioDetailVisibleBindingCount)",
    )
    for fragment in reserve_contracts:
        require_contains(factory, fragment, "FFT cache factory")

    compact_factory = compact_cpp(factory)
    prepared_bindings = (
        'append_audio_fft_binding( cache.fft_hidden_updates, '
        'std::string(kAudioFftBandsPath), "hidden", "true", 5U )',
        'append_audio_fft_binding( cache.fft_visible_updates, '
        'make_audio_fft_band_path(kAudioFftBandsPath, band), '
        '"height", {}, kAudioFftHeightValueCapacity )',
        'append_audio_fft_binding( cache.fft_visible_updates, '
        'std::string(kAudioFftBandsPath), "hidden", "false", 5U )',
        'append_audio_fft_binding( cache.detail_hidden_updates, '
        'std::string(kAudioDetailMic0BandsPath), "hidden", "true", 5U )',
        'append_audio_fft_binding( cache.detail_hidden_updates, '
        'std::string(kAudioDetailMic1BandsPath), "hidden", "true", 5U )',
        'append_audio_fft_binding( cache.detail_palette_updates, '
        'palette_path, "color", colors[channel], colors[channel].size() )',
        'append_audio_fft_binding( cache.detail_visible_updates, '
        'std::move(palette_path), "height", {}, '
        'kAudioFftHeightValueCapacity )',
        'append_audio_fft_binding( cache.detail_visible_updates, '
        'std::string(kAudioDetailMic0BandsPath), "hidden", "false", 5U )',
        'append_audio_fft_binding( cache.detail_visible_updates, '
        'std::string(kAudioDetailMic1BandsPath), "hidden", "false", 5U )',
    )
    for fragment in prepared_bindings:
        require_contains(
            compact_factory,
            fragment,
            "prepared FFT binding path/key/value order",
        )

    for fragment in (
        '"/LUNASHELL_audio_fft/surface/bands"',
        '"/LUNASHELL_audio_detail/surface/mic0_bands"',
        '"/LUNASHELL_audio_detail/surface/mic1_bands"',
        'absolute_path.append("/band")',
        "(band / 10U) % 10U",
        "band % 10U",
    ):
        require_contains(source, fragment, "FFT prepared path contract")

    require(
        initializer_colors(source, "kAudioDetailMic0Colors") == MIC0_COLORS,
        "microphone 0 FFT palette changed or was reordered",
    )
    require(
        initializer_colors(source, "kAudioDetailMic1Colors") == MIC1_COLORS,
        "microphone 1 FFT palette changed or was reordered",
    )

    outer_loop = factory.find(
        "band < LUNASHELL::kAudioSpectrumBandCount",
        factory.find("detail_palette_updates.reserve"),
    )
    channel_loop = factory.find(
        "channel < band_paths.size()",
        outer_loop,
    )
    palette_append = factory.find("cache.detail_palette_updates", channel_loop)
    height_append = factory.find("cache.detail_visible_updates", palette_append)
    require(
        0 <= outer_loop < channel_loop < palette_append < height_append,
        "detail bindings must remain band-major and channel-minor",
    )
    mic0_show = compact_factory.find(
        "std::string(kAudioDetailMic0BandsPath), \"hidden\", \"false\""
    )
    mic1_show = compact_factory.find(
        "std::string(kAudioDetailMic1BandsPath), \"hidden\", \"false\""
    )
    require(
        0 <= mic0_show < mic1_show,
        "detail visibility bindings must remain microphone 0 then microphone 1",
    )


def verify_lifecycle(source: str) -> None:
    on_start = braced_text(source, "App::on_start(", "LUNASHELL_app.cpp")
    on_stop = braced_text(source, "App::on_stop(", "LUNASHELL_app.cpp")
    for fragment in (
        "audio_fft_binding_cache_ == nullptr",
        "std::make_unique<app_detail::AudioFftBindingCache>",
        "make_audio_fft_binding_cache()",
        "catch (const std::bad_alloc &)",
        '"Failed to allocate Audio FFT binding cache"',
    ):
        require_contains(on_start, fragment, "App::on_start")
    require(
        on_start.find("make_audio_fft_binding_cache()")
        < on_start.find("LUNASHELL_liveness_watch_begin"),
        "FFT cache must be prepared before app startup can mount a lazy screen",
    )
    require_contains(
        on_stop,
        "audio_fft_binding_cache_.reset();",
        "App::on_stop",
    )
    forbid_contains(
        source,
        "static AudioFftBindingCache",
        "FFT cache lifecycle",
    )


def verify_value_formatter(source: str) -> None:
    formatter = braced_text(
        source,
        "assign_audio_fft_height(",
        "LUNASHELL_app.cpp",
    )
    compact_formatter = compact_cpp(formatter)
    for fragment in (
        "std::array<char, kAudioFftHeightValueCapacity + 1U>",
        '"%ddp"',
        "update.value.capacity()",
        "update.value.resize(",
        "std::memcpy( update.value.data()",
    ):
        require_contains(compact_formatter, fragment, "FFT height formatter")
    for fragment in ("std::to_string", ".append(", "operator new"):
        forbid_contains(formatter, fragment, "FFT height formatter")


def verify_refresh_contracts(source: str) -> None:
    fft = braced_text(
        source,
        "App::refresh_audio_fft(",
        "LUNASHELL_app.cpp",
    )
    detail = braced_text(
        source,
        "App::refresh_audio_detail(",
        "LUNASHELL_app.cpp",
    )

    for body, name in ((fft, "refresh_audio_fft"), (detail, "refresh_audio_detail")):
        for fragment in (
            "std::vector<esp_brookesia::gui::BindingValueUpdate>",
            "std::to_string(next_heights",
            "band%02u",
            "make_audio_fft_band_path(",
            "append_audio_fft_binding(",
            ".reserve(",
            ".push_back(",
        ):
            forbid_contains(body, fragment, f"{name} hot path")
        require_contains(
            body,
            "audio_fft_binding_cache_ == nullptr",
            f"{name} cache guard",
        )
        require_contains(
            body,
            "assign_audio_fft_height(",
            f"{name} allocation-free values",
        )
        require_contains(
            body,
            "height_change_count == 0U && !visibility_changed",
            f"{name} unchanged-frame fast path",
        )

    for fragment in (
        "SPECTRUM DISABLED (PERFORMANCE MODE)",
        "SPECTRUM UNAVAILABLE (SNAPSHOT NOT READY)",
        "SPECTRUM UNAVAILABLE (DIAGNOSTIC MONITOR OFF)",
        "SPECTRUM STALE (WAITING FOR NEW SAMPLE)",
        "SPECTRUM STALE (LATEST ATTEMPT FAILED)",
        "SPECTRUM ERROR (INVALID PCM OR FFT METADATA)",
        "SPECTRUM ERROR (BIN OUT OF RANGE)",
        "snapshot.audio.spectrum.last_attempt_at_us <=",
        "snapshot.audio.spectrum.last_success_at_us <=",
        "snapshot.audio.channel_spectrum[channel][band] > 100U",
    ):
        require_contains(fft, fragment, "standalone FFT validity contract")
        require_contains(detail, fragment, "detail FFT validity contract")

    for fragment in (
        "snapshot.audio.channel_count == 1U",
        "snapshot.audio.channel_spectrum[0][band]",
        "snapshot.audio.channel_spectrum[1][band]",
        "(average * 94 + 99) / 100",
        "set_cached_text(",
        '"/LUNASHELL_audio_fft/surface/spectrum_status"',
        "binding_cache.fft_hidden_updates",
        "binding_cache.fft_visible_updates",
    ):
        require_contains(fft, fragment, "standalone FFT behavior")
    require(
        fft.find("set_binding_values(context, spectrum_updates)")
        < fft.find("audio_fft_heights_ = next_heights"),
        "standalone FFT cache state must commit after successful dispatch",
    )
    fft_invalid = fft.find("if (!spectrum_valid)")
    fft_hide = fft.find("binding_cache.fft_hidden_updates", fft_invalid)
    fft_hidden_commit = fft.find("audio_fft_hidden_ = true", fft_hide)
    require(
        0 <= fft_invalid < fft_hide < fft_hidden_commit,
        "standalone FFT hidden state must commit after its cached dispatch",
    )

    for fragment in (
        "snapshot.audio.channel_count == 2U",
        "snapshot.audio.channel_spectrum[channel][band]",
        "* 64 + 99) / 100",
        "const size_t index = band * 2U + channel",
        "set_cached_text(",
        '"/LUNASHELL_audio_detail/surface/spectrum_status"',
        "binding_cache.detail_hidden_updates",
        "binding_cache.detail_palette_updates",
        "binding_cache.detail_visible_updates",
    ):
        require_contains(detail, fragment, "detail FFT behavior")
    require(
        detail.find("set_binding_values(context, spectrum_updates)")
        < detail.find("audio_detail_heights_ = next_heights"),
        "detail FFT cache state must commit after successful dispatch",
    )
    detail_invalid = detail.find("if (!spectrum_valid)")
    detail_hide = detail.find(
        "binding_cache.detail_hidden_updates",
        detail_invalid,
    )
    detail_hidden_commit = detail.find("audio_detail_hidden_ = true", detail_hide)
    require(
        0 <= detail_invalid < detail_hide < detail_hidden_commit,
        "detail FFT hidden state must commit after its cached dispatch",
    )
    palette_dispatch = detail.find(
        "binding_cache.detail_palette_updates",
        detail.find("if (!audio_detail_palette_applied_)"),
    )
    require(
        0 <= palette_dispatch
        < detail.find("audio_detail_palette_applied_ = true", palette_dispatch),
        "detail palette must be marked applied only after dispatch",
    )


def verify_runtime_coalescing(runtime: str) -> None:
    batch = braced_text(
        runtime,
        "void Runtime::Impl::set_binding_values(DocumentId document_id,",
        "Brookesia runtime_binding.cpp",
    )
    for fragment in (
        "const auto previous = store->get_string(",
        "previous.has_value() && *previous == update.value",
        "continue;",
        "store->set_string_silent(",
        "any_changed = true;",
        "if (!any_changed)",
    ):
        require_contains(batch, fragment, "Brookesia unchanged-binding filter")
    require(
        batch.find("previous.has_value() && *previous == update.value")
        < batch.find("store->set_string_silent("),
        "Brookesia must filter unchanged values before mutating the store",
    )


def main() -> int:
    try:
        source = read(APP_SOURCE)
        header = read(APP_HEADER)
        runtime = read(RUNTIME_SOURCE)
        verify_cache_layout(source, header)
        verify_lifecycle(source)
        verify_value_formatter(source)
        verify_refresh_contracts(source)
        verify_runtime_coalescing(runtime)
    except (AssertionError, OSError) as error:
        print(f"FFT UI cache verification: FAIL: {error}", file=sys.stderr)
        return 1

    print("FFT UI cache verification: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
