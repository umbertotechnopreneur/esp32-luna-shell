# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Shared synthetic payload vectors for the host and portable firmware EVENT decoders."""

from dataclasses import replace
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "plugins/cody-codex-plugin/mcp"))
from ccp_interactions import Choice, EventKind, EventPayload, PendingQuestion, Tone, encode_payload


# No parameters: constructs synthetic bounded vectors using the production host encoder.
# Returns (name, lowercase_hex, expected_valid) tuples; it performs no networking or storage.
def event_vectors() -> list[tuple[str, str, bool]]:
    choices = (Choice("memory", "Memory", "Keep the cache in memory.", Tone.POSITIVE),
               Choice("redis", "Redis", "Use the configured Redis cache.", Tone.INFO),
               Choice("none", "No cache", "Continue without a cache.", Tone.NEUTRAL))
    question = EventPayload(EventKind.QUESTION, "SIMULATED fixture", "Choose cache", "Which cache should I use?", choices)
    maximum = EventPayload(EventKind.QUESTION, "C" * 24, "T" * 24, "B" * 63,
        tuple(Choice(f"choice_{index}", str(index) + "L" * 62, "D" * 255, Tone(index)) for index in range(3)),
        pending_question_count=128,
        pending_questions=(PendingQuestion("1" * 32, "C" * 24, "T" * 24),
                           PendingQuestion("2" * 32, "C" * 24, "T" * 24)))
    failures = EventPayload(EventKind.CHECKS_FAILED, "SIMULATED fixture", "Checks failed", "Review the failed checks in Codex.",
                            failures=tuple(str(index) + "F" * 47 for index in range(6)),
                            progress_valid=True, passed=5, total=11)
    valid = [
        ("question_three", encode_payload(question), True),
        ("question_two", encode_payload(replace(question, choices=choices[:2])), True),
        ("question_selected", encode_payload(replace(question, selected_id="redis")), True),
        ("question_maximum", encode_payload(maximum), True),
        ("six_failure_names", encode_payload(failures), True),
        ("reply_ready_title_maximum", encode_payload(EventPayload(EventKind.REPLY_READY,
            "SIMULATED fixture", "Reply ready", "T" * 63)), True),
    ]
    for kind in (EventKind.NOTICE, EventKind.REPLY_READY, EventKind.APPROVAL_ON_PC, EventKind.PR_PENDING,
                 EventKind.CHECKS_RUNNING, EventKind.CHECKS_PASSED):
        payload = EventPayload(kind, "", "Fixture", "Synthetic decoder evidence.",
                               progress_valid=kind == EventKind.CHECKS_PASSED,
                               passed=2 if kind == EventKind.CHECKS_PASSED else 0,
                               total=2 if kind == EventKind.CHECKS_PASSED else 0)
        valid.append((kind.name.lower(), encode_payload(payload), True))
    base = bytes.fromhex(encode_payload(question))
    choice_offset = 2 + sum(1 + len(value) for value in (question.context, question.title, question.body))
    failure_bytes = bytes.fromhex(encode_payload(failures))
    failure_offset = 2 + sum(1 + len(value) for value in (failures.context, failures.title, failures.body)) + 2
    detail = choices[0].detail.encode("ascii")
    detail_and_tone = bytes((len(detail),)) + detail + bytes((int(choices[0].tone),))
    selected = bytes.fromhex(encode_payload(replace(question, selected_id="redis")))
    progress = bytes.fromhex(encode_payload(replace(failures, progress_valid=True, passed=1, total=8)))
    reply_progress = bytes.fromhex(encode_payload(EventPayload(EventKind.CHECKS_RUNNING,
        "", "Synthetic progress", "Decoder evidence.", progress_valid=True, passed=1, total=2)))
    # Each malformed vector changes the real encoded bytes, without asking the
    # encoder to accept an invalid object merely to construct a fixture.
    invalid = [
        ("unknown_version", (bytes((3,)) + base[1:]).hex(), False),
        ("retired_version", (bytes((1,)) + base[1:]).hex(), False),
        ("unknown_kind", (base[:1] + bytes((9,)) + base[2:]).hex(), False),
        ("reply_ready_with_choices", (base[:1] + bytes((8,)) + base[2:]).hex(), False),
        ("reply_ready_with_check_progress", (reply_progress[:1] + bytes((8,)) + reply_progress[2:]).hex(), False),
        ("truncated_tail", base[:-1].hex(), False),
        ("trailing_bytes", (base + b"\x00").hex(), False),
        ("oversized_context", (base[:2] + bytes((25,)) + base[3:]).hex(), False),
        ("duplicate_id", base.replace(b"\x05redis", b"\x06memory", 1).hex(), False),
        ("duplicate_label", base.replace(b"\x05Redis", b"\x06Memory", 1).hex(), False),
        ("non_ascii", base.replace(b"Choose cache", b"\xffhoose cache", 1).hex(), False),
        ("whitespace_title", base.replace(b"Choose cache", b" " * 12, 1).hex(), False),
        ("selected_unknown", selected.replace(b"\x05redis" + bytes(6), b"\x07unknown" + bytes(6), 1).hex(), False),
        ("invalid_tone", base.replace(detail_and_tone, detail_and_tone[:-1] + b"\x04", 1).hex(), False),
        ("too_many_choices", (base[:choice_offset] + b"\x04" + base[choice_offset + 1:]).hex(), False),
        ("too_many_failures", (failure_bytes[:failure_offset] + b"\x07" + failure_bytes[failure_offset + 1:]).hex(), False),
        ("invalid_progress_flag", (progress[:-7] + b"\x02" + progress[-6:]).hex(), False),
        ("invalid_progress_counts", (progress[:-6] + b"\x00\x03\x00\x02" + progress[-2:]).hex(), False),
        ("failure_progress_count_mismatch", (progress[:-6] + b"\x00\x01\x00\x02" + progress[-2:]).hex(), False),
        ("too_many_pending", (base[:-2] + b"\x81\x00").hex(), False),
        ("too_many_pending_entries", (base[:-2] + b"\x03\x03").hex(), False),
    ]
    return valid + invalid
