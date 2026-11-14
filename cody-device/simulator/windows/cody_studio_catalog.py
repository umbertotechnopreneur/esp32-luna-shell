"""Source catalog projection and explicit Studio contracts; never a renderer.

The maintained Markdown inventory owns the source identities. The JSON extension
adds executable contracts only, so Studio and export can share this projection
without independently maintaining a second list of screens.
"""

from copy import deepcopy
import json
from pathlib import Path
import re

from cody_native_runtime import ROOT

EXTENSION = Path(__file__).with_name("studio_catalog.json")
AVAILABILITIES = frozenset(("runtime-full", "presenter-isolated", "profile-conditional",
                            "unsupported", "not-applicable"))


# path: The maintained source inventory; defaults to the repository document.
# Raises OSError or ValueError for unreadable, empty or duplicate source identities.
def source_entries(path=None):
    path = Path(path) if path is not None else ROOT / "docs/cody/screen-catalog.md"
    entries = []
    seen = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("| "):
            continue
        cells = [cell.strip().replace("`", "") for cell in line.strip("|").split("|")]
        if len(cells) < 4 or not re.match(r"^(?:atlas|next|ui|widget)-v1:", cells[0]):
            continue
        key = cells[0].split(" / ", 1)[0].strip()
        namespace, values = key.split(":", 1)
        names = [value.strip() for value in values.split(",")]
        if len(names) == 1 and re.fullmatch(r"\d+[A-Z]-\d+[A-Z]", names[0]):
            names = names[0].split("-")
        for name in names:
            identity = f"{namespace}:{name}"
            if identity in seen:
                raise ValueError(f"Duplicate source catalog identity: {identity}")
            seen.add(identity)
            entries.append({"id": identity, "title": cells[1], "source_state": cells[-1],
                            "availability": "unsupported", "inventory_id": None,
                            "reason": "No native Studio preparation contract yet", "variants": [],
                            "routes": []})
    if not entries:
        raise ValueError("Source catalog contains no qualified screen identities")
    return entries


# No parameters: project source identities plus their explicit executable contracts.
# Raises OSError, JSON errors or ValueError for unknown/duplicate/invalid extensions.
def load_catalog():
    extension = json.loads(EXTENSION.read_text(encoding="utf-8"))
    if extension.get("schema") != "cody.studio.catalog.v1":
        raise ValueError("Unsupported Studio catalog schema")
    entries = {entry["id"]: entry for entry in source_entries()}
    declared = set()
    inventory_ids = set()
    for contract in extension["entries"]:
        identity = contract["id"]
        inventory_id = contract.get("inventory_id")
        if identity not in entries or identity in declared or (inventory_id and inventory_id in inventory_ids):
            raise ValueError(f"Unknown or duplicate Studio catalog contract: {identity}")
        if contract.get("availability") not in AVAILABILITIES:
            raise ValueError(f"Invalid Studio availability: {identity}")
        declared.add(identity)
        inventory_ids.add(inventory_id)
        entries[identity].update(deepcopy(contract))
        entries[identity]["inventory"] = deepcopy(extension["inventory"])
        entries[identity].pop("reason", None)
    return list(entries.values())


# identity: Qualified stable source identity or explicitly mapped inventory ID.
# Raises ValueError for unknown or ambiguous identities, without guessing enum values.
def entry_for(identity):
    matches = [entry for entry in load_catalog()
               if identity == entry["id"] or (entry["inventory_id"] is not None and identity == entry["inventory_id"])]
    if len(matches) != 1:
        raise ValueError(f"Unknown or ambiguous Studio screen identity: {identity!r}")
    return matches[0]


# entry: Validated catalog entry with a repository-relative source file.
# symbol: Optional current symbol; defaults to the entry's presenter symbol.
# Raises OSError or ValueError when source/symbol cannot be resolved in this checkout.
def resolve_source(entry, symbol=None):
    source = (ROOT / entry["source"]).resolve(strict=True)
    if not source.is_relative_to(ROOT):
        raise ValueError("Studio source escaped the repository")
    name = symbol or entry["symbol"]
    pattern = re.compile(r"\b" + re.escape(name) + r"\s*\(")
    for number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), 1):
        if pattern.search(line):
            return source, number
    raise ValueError(f"Source symbol is absent in current checkout: {name}")
