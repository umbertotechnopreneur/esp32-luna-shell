# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added bounded technical metadata on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested truthful companion telemetry.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Bounded technical metadata projection; no transcript or desktop session acquisition."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import re
from ccp_pricing import CostBand, MAX_DAY_MS


FIELDS = ("device", "pc", "nonce", "chat", "turn", "revision", "model", "effort", "modelage",
          "compaction", "compactseq", "compactage", "activityage", "turnelapsed",
          "used", "capacity", "cumulative", "input", "output", "threshold", "usageage",
          "cost", "costage", "costttl")
UNAVAILABLE_FIELDS = ("effort", "used", "capacity", "cumulative", "input", "output", "threshold", "usageage")


class CompactionState(str, Enum):
    NONE = "none"
    ACTIVE = "active"
    REFRESHED = "refreshed"
    ORPHANED = "orphaned"
    INTERRUPTED = "interrupted"
    STALE = "stale"


@dataclass(frozen=True)
class TechnicalProjection:
    chat: str
    turn: str
    revision: str
    model: str = ""
    model_age_ms: int | None = None
    compaction: CompactionState = CompactionState.NONE
    compaction_sequence: int = 0
    compaction_age_ms: int | None = None
    activity_age_ms: int | None = None
    turn_elapsed_ms: int | None = None
    # The hooked slug is reported metadata; it does not prove a separately executed/rerouted model.
    model_source: str = "hook-metadata"
    effort: str = ""
    context_used: int | None = None
    context_capacity: int | None = None
    cumulative_tokens: int | None = None
    input_tokens: int | None = None
    output_tokens: int | None = None
    compact_threshold: int | None = None
    usage_age_ms: int | None = None
    cost_band: CostBand = CostBand.UNKNOWN
    cost_age_ms: int | None = None
    cost_valid_for_ms: int | None = None


# value: Exact decimal uint32 milliseconds or the explicit unavailable token.
# name: Nonsecret protocol field identity used in rejection diagnostics.
# Raises ValueError for signs, padding, overflow or unsupported age encoding.
def milliseconds(value: str, name: str) -> int | None:
    if value == "-":
        return None
    if re.fullmatch(r"0|[1-9][0-9]{0,9}", value) is None or int(value) > 0xffffffff:
        raise ValueError(f"TECH {name} must be uint32 milliseconds or unavailable")
    return int(value)


# fields: Strict ordered authenticated TECH fields; authenticity remains the transport owner's responsibility.
# Raises ValueError for unsupported fields, identities, model metadata or fabricated metrics.
def decode_projection(fields: dict[str, str]) -> TechnicalProjection:
    if tuple(fields) != (*FIELDS, "proof"):
        raise ValueError("TECH fields must follow the fixed authenticated order")
    for name, width in (("chat", 64), ("turn", 64), ("revision", 16), ("compactseq", 16)):
        if re.fullmatch(r"[0-9a-f]{" + str(width) + "}", fields[name]) is None:
            raise ValueError(f"TECH {name} identity encoding is malformed")
    if any(fields[name] != "-" for name in UNAVAILABLE_FIELDS):
        raise ValueError("TECH effort and token metrics have no connected authoritative source")
    model = ""
    if fields["model"] != "-":
        encoded = fields["model"]
        if re.fullmatch(r"(?:[0-9a-f]{2}){1,128}", encoded) is None:
            raise ValueError("TECH model encoding exceeds its bounded full identity")
        model = bytes.fromhex(encoded).decode("ascii")
        if any(not 33 <= ord(character) <= 126 for character in model):
            raise ValueError("TECH model is not printable ASCII metadata")
    model_age = milliseconds(fields["modelage"], "modelage")
    compact_age = milliseconds(fields["compactage"], "compactage")
    activity_age = milliseconds(fields["activityage"], "activityage")
    turn_elapsed = milliseconds(fields["turnelapsed"], "turnelapsed")
    cost = CostBand(fields["cost"])
    cost_age = milliseconds(fields["costage"], "costage")
    cost_ttl = milliseconds(fields["costttl"], "costttl")
    compaction = CompactionState(fields["compaction"])
    sequence = int(fields["compactseq"], 16)
    if bool(model) != (model_age is not None):
        raise ValueError("TECH model metadata and source age are incoherent")
    if cost == CostBand.UNKNOWN:
        if cost_age is not None or cost_ttl is not None:
            raise ValueError("TECH Unknown cost must not claim current catalog durations")
    elif (not model or model_age is None or model_age >= 120000 or cost_age is None
          or cost_age > MAX_DAY_MS or cost_ttl is None or not 0 < cost_ttl <= MAX_DAY_MS):
        raise ValueError("TECH known cost requires a current model and bounded catalog age/expiry")
    if (compaction == CompactionState.NONE) != (sequence == 0) or sequence == 0 and compact_age is not None:
        raise ValueError("TECH compaction identity and source age are incoherent")
    if fields["chat"] == "0" * 64:
        if (fields["turn"] != "0" * 64 or model or sequence or activity_age is not None or turn_elapsed is not None):
            raise ValueError("TECH empty followed context is incoherent")
    elif fields["turn"] == "0" * 64 and (model or sequence or turn_elapsed is not None):
        raise ValueError("TECH turn-scoped metadata requires its observed turn identity")
    return TechnicalProjection(fields["chat"], fields["turn"], fields["revision"], model,
        model_age, compaction, sequence, compact_age, activity_age, turn_elapsed,
        cost_band=cost, cost_age_ms=cost_age, cost_valid_for_ms=cost_ttl)
