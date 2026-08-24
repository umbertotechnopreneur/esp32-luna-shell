"""Static lifecycle and allocation checks for lazy GUI documents."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
BROOKESIA = ROOT / "third_party" / "esp-brookesia"
APP_COMPONENT = ROOT / "components" / "LUNASHELL_app"
RUNTIME_PACKAGE = APP_COMPONENT / "package"
SDKCONFIG_DEFAULTS = ROOT / "sdkconfig.defaults"

EXPECTED_MONTSERRAT_SIZES = {"10", "12", "16", "20", "24", "32"}
EXPECTED_TYPOGRAPHY_AVAILABLE = [
    "10sp",
    "12sp",
    "16sp",
    "20sp",
    "24sp",
    "32sp",
]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_contains(text: str, fragment: str, source: str) -> None:
    require(fragment in text, f"{fragment!r} missing from {source}")


def require_config_value(defaults: str, name: str, expected: str) -> None:
    prefix = f"{name}="
    values = [
        line[len(prefix) :].strip()
        for line in defaults.splitlines()
        if line.startswith(prefix)
    ]
    require(
        values == [expected],
        f"sdkconfig.defaults must set {name}={expected} exactly once; "
        f"found {values or 'no assignment'}",
    )


def load_json(path: Path) -> Any:
    try:
        return json.loads(read(path))
    except json.JSONDecodeError as error:
        relative = path.relative_to(ROOT).as_posix()
        raise AssertionError(
            f"invalid JSON {relative}:{error.lineno}:{error.colno}: "
            f"{error.msg}"
        ) from error


def named_json_values(
    value: Any,
    name: str,
    location: str = "$",
):
    if isinstance(value, dict):
        for key, child in value.items():
            child_location = f"{location}.{key}"
            if key == name:
                yield child_location, child
            yield from named_json_values(child, name, child_location)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from named_json_values(child, name, f"{location}[{index}]")


def braced_initializers(text: str, marker: str) -> list[tuple[int, str]]:
    initializers: list[tuple[int, str]] = []
    cursor = 0
    while True:
        marker_at = text.find(marker, cursor)
        if marker_at < 0:
            return initializers
        brace_at = text.find("{", marker_at + len(marker) - 1)
        require(brace_at >= 0, f"missing initializer brace after {marker!r}")
        depth = 0
        quote = ""
        escaped = False
        for index in range(brace_at, len(text)):
            token = text[index]
            if quote:
                if escaped:
                    escaped = False
                elif token == "\\":
                    escaped = True
                elif token == quote:
                    quote = ""
                continue
            if token in {'"', "'"}:
                quote = token
            elif token == "{":
                depth += 1
            elif token == "}":
                depth -= 1
                if depth == 0:
                    initializers.append((brace_at, text[brace_at : index + 1]))
                    cursor = index + 1
                    break
        else:
            raise AssertionError(f"unterminated initializer after {marker!r}")


def cpp_code_only(text: str) -> str:
    output = list(text)
    index = 0
    state = "code"
    while index < len(text):
        token = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if token == "/" and following == "/":
                output[index] = output[index + 1] = " "
                index += 2
                state = "line_comment"
                continue
            if token == "/" and following == "*":
                output[index] = output[index + 1] = " "
                index += 2
                state = "block_comment"
                continue
            if token == '"':
                output[index] = " "
                index += 1
                state = "string"
                continue
            if token == "'":
                output[index] = " "
                index += 1
                state = "character"
                continue
            index += 1
            continue
        if state == "line_comment":
            if token == "\n":
                state = "code"
            else:
                output[index] = " "
            index += 1
            continue
        if state == "block_comment":
            if token == "*" and following == "/":
                output[index] = output[index + 1] = " "
                index += 2
                state = "code"
            else:
                if token != "\n":
                    output[index] = " "
                index += 1
            continue
        if token == "\\" and index + 1 < len(text):
            output[index] = " "
            if text[index + 1] != "\n":
                output[index + 1] = " "
            index += 2
            continue
        output[index] = "\n" if token == "\n" else " "
        if (state == "string" and token == '"') or (
            state == "character" and token == "'"
        ):
            state = "code"
        index += 1
    return "".join(output)


def main() -> int:
    manager = read(
        BROOKESIA
        / "system"
        / "brookesia_system_core"
        / "src"
        / "app"
        / "manager.cpp"
    )
    timer = read(
        BROOKESIA
        / "system"
        / "brookesia_system_core"
        / "src"
        / "system"
        / "timer.cpp"
    )
    runtime = read(
        BROOKESIA
        / "gui"
        / "brookesia_gui_interface"
        / "src"
        / "runtime.cpp"
    )
    props = read(
        BROOKESIA
        / "gui"
        / "brookesia_gui_lvgl"
        / "src"
        / "props.cpp"
    )
    types = read(
        BROOKESIA
        / "gui"
        / "brookesia_gui_lvgl"
        / "src"
        / "private"
        / "types.hpp"
    )
    style = read(
        BROOKESIA
        / "gui"
        / "brookesia_gui_lvgl"
        / "src"
        / "style.cpp"
    )
    signal = read(
        BROOKESIA
        / "utils"
        / "brookesia_lib_utils"
        / "include"
        / "brookesia"
        / "lib_utils"
        / "signal.hpp"
    )
    app = read(APP_COMPONENT / "LUNASHELL_app.cpp")
    app_documents = read(APP_COMPONENT / "LUNASHELL_app_documents.cpp")
    sdkconfig_defaults = read(SDKCONFIG_DEFAULTS)

    stop_start = manager.index(
        "std::expected<void, std::string> System::stop_app(AppId app_id)"
    )
    stop_end = manager.index(
        "std::expected<void, std::string> System::pause_app(",
        stop_start,
    )
    stop_body = manager[stop_start:stop_end]
    on_stop_index = stop_body.index("record.native_app->on_stop(")
    fallback_cancel_index = stop_body.index(
        "impl_->cancel_app_timers(record);",
        on_stop_index,
    )
    require(
        on_stop_index < fallback_cancel_index,
        "framework timer cleanup must run after native on_stop",
    )
    require(
        stop_body.index("record.info.state = AppState::Stopping;")
        < on_stop_index,
        "Stopping state must block timer dispatch before on_stop",
    )
    require_contains(
        timer,
        "if (record.info.state != AppState::Running)",
        "timer dispatch guard",
    )

    require_contains(
        types,
        "std::array<std::shared_ptr<KeyboardLayoutStorage>, 4> keyboard_layouts;",
        "per-node keyboard layout ownership",
    )
    require(
        "keyboard_layout_backing_store" not in types
        and "keyboard_layout_backing_store" not in props,
        "keyboard layout storage must not accumulate in a backend-global owner",
    )
    require_contains(
        props,
        "record.keyboard_layouts[*index] = std::move(storage_owner);",
        "per-node keyboard layout ownership",
    )

    for fragment in (
        "std::weak_ptr<SlotOwner> owner;",
        "connected.exchange(false, std::memory_order_acq_rel)",
        "locked_owner->disconnect_slot(this);",
        "std::shared_ptr<Slot> disconnected;",
        "disconnected = std::move(*it);",
        "disconnected.swap(state->slots);",
    ):
        require_contains(signal, fragment, "immediate signal-slot release")
    require(
        "void prune() const" not in signal,
        "signal disconnect must not defer release until a future emission",
    )
    require(
        signal.index("disconnected = std::move(*it);")
        < signal.index("slots.erase(it);"),
        "slot callback must move out before erasing under the registry lock",
    )

    require_contains(
        style,
        "kMaxRememberedFontLogMessages = 128",
        "bounded font-log retention",
    )
    require_contains(
        style,
        "#define log_font_info_once(message) ((void)0)",
        "compile-time disabled font debug logging",
    )

    unload_start = runtime.index("bool unload(DocumentId document_id)")
    unload_end = runtime.index("void set_view_debug_enabled(", unload_start)
    unload_body = runtime[unload_start:unload_end]
    for old_temporary in (
        "event_animation_keys",
        "style_cache_keys_to_erase",
        "mounted_targets_to_erase",
        "route_keys",
        "stale_subscriptions",
    ):
        require(
            old_temporary not in unload_body,
            f"unload still allocates temporary key list {old_temporary}",
        )
    require_contains(
        unload_body,
        "resolved_style_cache_.erase(erase_it);",
        "in-place style cache release",
    )

    child_error = runtime.index("if (!child_uid)", runtime.index("create_subtree("))
    rollback = runtime.index("destroy_subtree(tree, uid);", child_error)
    propagate = runtime.index("return std::unexpected(child_uid.error());", child_error)
    require(
        child_error < rollback < propagate,
        "partial subtree must be destroyed before propagating a child failure",
    )

    restore_start = app_documents.index(
        "std::expected<void, std::string> App::restore_welcome_document("
    )
    restore_end = app_documents.index(
        "esp_brookesia::system::core::AppManifest App::get_manifest()",
        restore_start,
    )
    restore = app_documents[restore_start:restore_end]
    welcome_trigger = restore.index("trigger_screen_flow(")
    lazy_unload = restore.index("unload_active_screen_document(context)")
    post_unload_failure = restore.index("if (!welcome_result)", lazy_unload)
    require(
        welcome_trigger < lazy_unload < post_unload_failure,
        "Welcome rollback must always attempt lazy-document unload",
    )

    for fragment in (
        "hardware_ui_cache_.statuses[index]",
        "hardware_ui_cache_.colors[index]",
        "imu_ui_cache_.status",
        "imu_ui_cache_.arc_opacity[axis]",
        "gps_ui_cache_.device_state",
        "gps_ui_cache_.satellite_signal",
    ):
        require_contains(app, fragment, "differential UI refresh")
    require(
        "LUNASHELL_LAZY_GUI_STRESS" not in app
        and "Lazy GUI stress" not in app,
        "diagnostic navigation harness must not ship in production",
    )

    require(RUNTIME_PACKAGE.is_dir(), "LUNASHELL runtime package is missing")
    non_json_runtime_files = sorted(
        path.relative_to(ROOT).as_posix()
        for path in RUNTIME_PACKAGE.rglob("*")
        if path.is_file() and path.suffix.lower() != ".json"
    )
    require(
        not non_json_runtime_files,
        "LUNASHELL runtime package must contain JSON documents only; found "
        f"external payloads: {non_json_runtime_files}",
    )

    json_sources = sorted((APP_COMPONENT / "gui").rglob("*.json"))
    json_sources.extend(sorted(RUNTIME_PACKAGE.rglob("*.json")))
    canonical_gui = load_json(APP_COMPONENT / "gui" / "root.json")
    require(isinstance(canonical_gui, dict), "canonical GUI must be an object")
    canonical_assets = canonical_gui.get("assets")
    require(isinstance(canonical_assets, list), "canonical GUI assets missing")
    constant_assets = [
        asset
        for asset in canonical_assets
        if isinstance(asset, dict) and asset.get("type") == "constant"
    ]
    require(
        len(constant_assets) == 1,
        "canonical GUI must define exactly one constant asset",
    )
    constant_data = constant_assets[0].get("data")
    require(isinstance(constant_data, dict), "constant asset data missing")
    typography = constant_data.get("typography")
    require(isinstance(typography, dict), "typography constants missing")
    require(
        typography.get("available") == EXPECTED_TYPOGRAPHY_AVAILABLE,
        "typography.available must match the compiled Montserrat sizes exactly",
    )

    invalid_font_references: list[str] = []
    for path in json_sources:
        document = load_json(path)
        relative = path.relative_to(ROOT).as_posix()
        require(isinstance(document, dict), f"{relative} must be an object")
        document_assets = document.get("assets", [])
        require(
            isinstance(document_assets, list),
            f"{relative} assets must be an array",
        )
        local_constant_assets = [
            asset
            for asset in document_assets
            if isinstance(asset, dict) and asset.get("type") == "constant"
        ]
        require(
            len(local_constant_assets) <= 1,
            f"{relative} defines duplicate constant assets",
        )
        document_typography = typography
        if local_constant_assets:
            local_data = local_constant_assets[0].get("data")
            require(
                isinstance(local_data, dict),
                f"{relative} constant asset data missing",
            )
            local_typography = local_data.get("typography")
            require(
                local_typography == typography,
                f"{relative} typography constants drifted from canonical GUI",
            )
            document_typography = local_typography
        for location, font_size in named_json_values(document, "fontSize"):
            if not isinstance(font_size, str):
                invalid_font_references.append(
                    f"{relative}:{location} non-string {font_size!r}"
                )
                continue
            literal = re.fullmatch(r"([0-9]+)sp", font_size)
            if literal is not None:
                if literal.group(1) not in EXPECTED_MONTSERRAT_SIZES:
                    invalid_font_references.append(
                        f"{relative}:{location} {font_size}"
                    )
                continue
            reference = re.fullmatch(
                r"\$\{constant\.typography\.([A-Za-z_][A-Za-z0-9_]*)\}",
                font_size,
            )
            if reference is None:
                invalid_font_references.append(
                    f"{relative}:{location} unresolved {font_size!r}"
                )
                continue
            resolved = document_typography.get(reference.group(1))
            resolved_literal = (
                re.fullmatch(r"([0-9]+)sp", resolved)
                if isinstance(resolved, str)
                else None
            )
            if (
                resolved_literal is None
                or resolved_literal.group(1) not in EXPECTED_MONTSERRAT_SIZES
            ):
                invalid_font_references.append(
                    f"{relative}:{location} {font_size!r} -> {resolved!r}"
                )
    require(
        not invalid_font_references,
        "GUI fontSize values must resolve through canonical typography to a "
        f"compiled Montserrat size: {invalid_font_references}",
    )

    runtime_image_initializers: list[tuple[Path, str, int, str]] = []
    for path in sorted(APP_COMPONENT.rglob("*.cpp")):
        source = read(path)
        for initializer_at, initializer in braced_initializers(
            source,
            "RuntimeImageResource{",
        ):
            runtime_image_initializers.append(
                (path, initializer, initializer_at, source)
            )
    require(
        runtime_image_initializers,
        "application must register at least one native runtime image",
    )
    invalid_runtime_images: list[str] = []
    for path, initializer, initializer_at, source in runtime_image_initializers:
        relative = path.relative_to(ROOT).as_posix()
        if re.search(r'\.primary_src\s*=\s*""\s*,', initializer) is None:
            invalid_runtime_images.append(f"{relative}:primary_src")
        native_source = re.search(
            r"\.native_src\s*=\s*reinterpret_cast<uintptr_t>\(\s*"
            r"([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*,",
            initializer,
        )
        if native_source is None:
            invalid_runtime_images.append(f"{relative}:native_src")
            continue
        descriptor_name = native_source.group(1)
        preceding_code = cpp_code_only(
            source[max(0, initializer_at - 1200) : initializer_at]
        )
        if re.search(
            rf"if\s*\(\s*{re.escape(descriptor_name)}\s*==\s*nullptr\s*\)",
            preceding_code,
        ) is None:
            invalid_runtime_images.append(
                f"{relative}:unchecked {descriptor_name}"
            )
    require(
        not invalid_runtime_images,
        "Runtime images must have an empty external source and a native LVGL "
        f"descriptor: {invalid_runtime_images}",
    )

    manifest_start = app_documents.index(
        "esp_brookesia::system::core::AppManifest App::get_manifest() const"
    )
    descriptor_start = app_documents.index(
        "esp_brookesia::system::core::AppGuiDescriptor "
        "App::get_gui_descriptor() const",
        manifest_start,
    )
    descriptor_end = app_documents.index(
        "\n\n}  // namespace LUNASHELL",
        descriptor_start,
    )
    manifest_body = app_documents[manifest_start:descriptor_start]
    descriptor_body = app_documents[descriptor_start:descriptor_end]
    for field in ("icon_id", "icon_path"):
        assignments = re.findall(rf"\.{field}\s*=", manifest_body)
        require(
            len(assignments) == 1
            and re.search(rf'\.{field}\s*=\s*""\s*,', manifest_body)
            is not None,
            f"App manifest {field} must be assigned one empty string",
        )
    require(
        len(re.findall(r"\.resources\s*=", descriptor_body)) == 1
        and re.search(r"\.resources\s*=\s*\{\s*\}\s*,", descriptor_body)
        is not None,
        "App GUI descriptor resources must be assigned one empty initializer",
    )

    application_sources = {
        suffix: sorted(APP_COMPONENT.rglob(f"*{suffix}"))
        for suffix in (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx")
    }
    forbidden_call_pattern = re.compile(
        r"\b(register_font_file|mount_font_assets|mount_image_assets|"
        r"lv_snapshot_[A-Za-z0-9_]*)\s*\("
    )
    forbidden_calls: list[str] = []
    for paths in application_sources.values():
        for path in paths:
            for match in forbidden_call_pattern.finditer(
                cpp_code_only(read(path))
            ):
                forbidden_calls.append(
                    f"{path.relative_to(ROOT).as_posix()}:{match.group(1)}"
                )
    require(
        not forbidden_calls,
        "LUNASHELL application must not call external-media or snapshot APIs: "
        f"{forbidden_calls}",
    )

    for name in (
        "CONFIG_ESP_LVGL_ADAPTER_ENABLE_DECODER",
        "CONFIG_ESP_LVGL_ADAPTER_ENABLE_FS",
        "CONFIG_LV_USE_LODEPNG",
        "CONFIG_LV_USE_SNAPSHOT",
    ):
        require_config_value(sdkconfig_defaults, name, "n")

    montserrat_assignments = re.findall(
        r"^CONFIG_LV_FONT_MONTSERRAT_([A-Z0-9_]+)=(\S+)$",
        sdkconfig_defaults,
        flags=re.MULTILINE,
    )
    montserrat_values: dict[str, list[str]] = {}
    for size, value in montserrat_assignments:
        montserrat_values.setdefault(size, []).append(value)
    duplicate_montserrat = {
        size: values
        for size, values in montserrat_values.items()
        if len(values) != 1
    }
    require(
        not duplicate_montserrat,
        "sdkconfig.defaults has duplicate Montserrat assignments: "
        f"{duplicate_montserrat}",
    )
    enabled_montserrat = {
        size for size, values in montserrat_values.items() if values == ["y"]
    }
    require(
        enabled_montserrat == EXPECTED_MONTSERRAT_SIZES,
        "sdkconfig.defaults must enable only Montserrat sizes "
        f"{sorted(EXPECTED_MONTSERRAT_SIZES, key=int)}; found "
        f"{sorted(enabled_montserrat)}",
    )

    default_font_assignments = re.findall(
        r"^CONFIG_LV_FONT_DEFAULT_([A-Z0-9_]+)=(\S+)$",
        sdkconfig_defaults,
        flags=re.MULTILINE,
    )
    default_font_values: dict[str, list[str]] = {}
    for name, value in default_font_assignments:
        default_font_values.setdefault(name, []).append(value)
    duplicate_default_fonts = {
        name: values
        for name, values in default_font_values.items()
        if len(values) != 1
    }
    require(
        not duplicate_default_fonts,
        "sdkconfig.defaults has duplicate default-font assignments: "
        f"{duplicate_default_fonts}",
    )
    enabled_default_fonts = {
        name
        for name, values in default_font_values.items()
        if values == ["y"]
    }
    require(
        enabled_default_fonts == {"MONTSERRAT_16"},
        "sdkconfig.defaults must select only Montserrat 16 as the default font; "
        f"found {sorted(enabled_default_fonts)}",
    )

    print(
        "lazy runtime contract verification: PASS "
        "(timers, signal release, bounded logs, unload, rollback, UI caches, "
        "no external media)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
