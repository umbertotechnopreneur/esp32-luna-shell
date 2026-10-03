"""Stable provenance for authored UTF-8 text across Windows and Unix checkouts."""

import hashlib
from pathlib import Path


# path: Required UTF-8 authoring file; only platform line endings are normalized.
# Raises OSError or UnicodeError for missing or invalid source text.
def text_source_hash(path: Path) -> str:
    source = path.read_text(encoding="utf-8")
    return hashlib.sha256(source.encode("utf-8")).hexdigest()
