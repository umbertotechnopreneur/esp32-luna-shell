#!/usr/bin/env python3
"""Verify TOF400F Modbus vectors without requiring ESP-IDF or hardware."""

from __future__ import annotations

from pathlib import Path


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def append_crc(payload: bytes) -> bytes:
    return payload + crc16_modbus(payload).to_bytes(2, "little")


def saturating_u32_add(value: int, amount: int) -> int:
    return min(value + amount, 0xFFFFFFFF)


def parse_holding_register(frame: bytes) -> int:
    if len(frame) != 7:
        raise ValueError("unexpected frame length")
    if frame[0:3] != bytes.fromhex("01 03 02"):
        raise ValueError("unexpected address, function, or byte count")
    if crc16_modbus(frame[:-2]) != int.from_bytes(frame[-2:], "little"):
        raise ValueError("CRC mismatch")
    return int.from_bytes(frame[3:5], "big")


def parse_distance(frame: bytes) -> int:
    distance_mm = parse_holding_register(frame)
    if not 1 <= distance_mm <= 4000:
        raise ValueError("distance outside documented TOF400F range")
    return distance_mm


def verify_protocol_source_is_host_pure() -> None:
    component_dir = Path(__file__).resolve().parents[1]
    source = (component_dir / "qsee_tof400f_protocol.c").read_text(
        encoding="utf-8"
    )
    forbidden_includes = (
        '#include "driver/',
        '#include "esp_',
        '#include "freertos/',
    )
    for forbidden in forbidden_includes:
        if forbidden in source:
            raise AssertionError(
                f"protocol helper gained target-only dependency: {forbidden}"
            )


def verify_driver_source_invariants() -> None:
    component_dir = Path(__file__).resolve().parents[1]
    source = (component_dir / "qsee_tof400f.c").read_text(
        encoding="utf-8"
    )
    transaction_start = source.index(
        "static esp_err_t transact_pair_locked("
    )
    transaction_end = source.index(
        "static bool responses_have_uart_fault(",
        transaction_start,
    )
    transaction = source[transaction_start:transaction_end]

    reset_index = transaction.index("*requested_at_us = 0;")
    wait_index = transaction.index("uart_wait_tx_done(")
    wait_failure_index = transaction.index(
        "if (transmit_error != ESP_OK)"
    )
    stamp_index = transaction.index(
        "const int64_t transmitted_at_us = esp_timer_get_time();"
    )
    publish_index = transaction.index(
        "*requested_at_us = transmitted_at_us;"
    )
    assert (
        reset_index
        < wait_index
        < wait_failure_index
        < stamp_index
        < publish_index
    ), "shared request timestamp can be published before TX completes"
    assert transaction.count("*requested_at_us =") == 2, (
        "shared request timestamp gained an unverified assignment path"
    )

    cleanup_start = source.index(
        "static esp_err_t cleanup_transport_locked("
    )
    cleanup_end = source.index(
        "static esp_err_t install_uart_slot_locked(",
        cleanup_start,
    )
    cleanup = source[cleanup_start:cleanup_end]
    delete_index = cleanup.index("uart_driver_delete(uart)")
    clear_handle_index = cleanup.index(
        "slot->event_queue = NULL;",
        delete_index,
    )
    assert delete_index < clear_handle_index
    assert cleanup[delete_index:clear_handle_index].count(
        "event_queue"
    ) == 0, "UART event queue handle is used after driver deletion"
    assert "for (size_t reverse_index = QSEE_TOF400F_SENSOR_COUNT;" in (
        cleanup
    )

    install_start = source.index(
        "static esp_err_t install_uart_slot_locked("
    )
    install_end = source.index(
        "static esp_err_t prepare_transport_locked(",
        install_start,
    )
    install = source[install_start:install_end]
    failed_install = install[
        install.index("if (error != ESP_OK)") :
        install.index("slot->owned = true;", install.index("return error;"))
    ]
    assert (
        failed_install.index("uart_is_driver_installed(uart)")
        < failed_install.index("slot->owned = true;")
        < failed_install.index("return error;")
    ), "partial UART install can escape without owned cleanup state"

    assert "static void add_stats_counter_saturated(" in source
    assert "UINT32_MAX - *counter < amount" in source
    assert "*counter = UINT32_MAX;" in source
    assert "++s_stats[" not in source, (
        "driver statistics gained a wrapping increment"
    )
    assert "s_stats[sensor_index].uart_errors +=" not in source, (
        "batched UART statistics bypass the saturating helper"
    )
    for field in (
        "frames_ok",
        "crc_errors",
        "protocol_errors",
        "timeouts",
        "uart_errors",
    ):
        assert f"&s_stats[sensor_index].{field}" in source, (
            f"{field} no longer uses the bounded statistics path"
        )


def main() -> None:
    assert saturating_u32_add(0, 1) == 1
    assert saturating_u32_add(0xFFFFFFFE, 1) == 0xFFFFFFFF
    assert saturating_u32_add(0xFFFFFFFF, 1) == 0xFFFFFFFF
    assert saturating_u32_add(0xFFFFFFFE, 8) == 0xFFFFFFFF

    official_read_request = bytes.fromhex("01 03 00 10 00 01 85 CF")
    official_response_21_mm = bytes.fromhex("01 03 02 00 15 79 8B")
    assert append_crc(bytes.fromhex("01 03 00 10 00 01")) == (
        official_read_request
    )
    assert parse_distance(official_response_21_mm) == 21
    assert append_crc(bytes.fromhex("01 03 00 05 00 01")) == bytes.fromhex(
        "01 03 00 05 00 01 94 0B"
    )
    assert append_crc(bytes.fromhex("01 03 00 04 00 01")) == bytes.fromhex(
        "01 03 00 04 00 01 C5 CB"
    )
    assert parse_holding_register(
        bytes.fromhex("01 03 02 00 00 B8 44")
    ) == 0
    assert parse_holding_register(
        bytes.fromhex("01 03 02 00 01 79 84")
    ) == 1

    response_1000_mm = append_crc(bytes.fromhex("01 03 02 03 E8"))
    assert parse_distance(response_1000_mm) == 1000
    assert append_crc(bytes.fromhex("01 06 00 05 00 00")) == bytes.fromhex(
        "01 06 00 05 00 00 99 CB"
    )
    assert append_crc(bytes.fromhex("01 06 00 04 00 01")) == bytes.fromhex(
        "01 06 00 04 00 01 09 CB"
    )

    corrupted = bytearray(response_1000_mm)
    corrupted[-1] ^= 0x01
    try:
        parse_distance(bytes(corrupted))
    except ValueError as error:
        assert str(error) == "CRC mismatch"
    else:
        raise AssertionError("corrupted CRC was accepted")

    verify_protocol_source_is_host_pure()
    verify_driver_source_invariants()
    print("TOF400F protocol verification passed")


if __name__ == "__main__":
    main()
