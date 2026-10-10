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

"""Stable provenance for authored UTF-8 text across Windows and Unix checkouts."""

import hashlib
from pathlib import Path


# path: Required UTF-8 authoring file; only platform line endings are normalized.
# Raises OSError or UnicodeError for missing or invalid source text.
def text_source_hash(path: Path) -> str:
    source = path.read_text(encoding="utf-8")
    return hashlib.sha256(source.encode("utf-8")).hexdigest()
