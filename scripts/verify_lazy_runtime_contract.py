"""Static lifecycle and allocation checks for lazy GUI documents."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BROOKESIA = ROOT / "third_party" / "esp-brookesia"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_contains(text: str, fragment: str, source: str) -> None:
    require(fragment in text, f"{fragment!r} missing from {source}")


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
    app = read(ROOT / "components" / "qsee_app" / "qsee_app.cpp")

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

    restore_start = app.index(
        "std::expected<void, std::string> App::restore_welcome_document("
    )
    restore_end = app.index(
        "esp_brookesia::system::core::AppManifest App::get_manifest()",
        restore_start,
    )
    restore = app[restore_start:restore_end]
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
        "QSEE_LAZY_GUI_STRESS" not in app
        and "Lazy GUI stress" not in app,
        "diagnostic navigation harness must not ship in production",
    )

    print(
        "lazy runtime contract verification: PASS "
        "(timers, signal release, bounded logs, unload, rollback, UI caches)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
