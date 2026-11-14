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
RENDERING_MODES = frozenset(("runtime-full", "presenter-isolated"))


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
        if (contract.get("availability") in RENDERING_MODES and
                contract.get("rendering_mode") != contract["availability"]):
            raise ValueError(f"Studio rendering mode and availability disagree: {identity}")
        declared.add(identity)
        inventory_ids.add(inventory_id)
        entries[identity].update(deepcopy(contract))
        entries[identity]["inventory"] = deepcopy(extension["inventory"])
        entries[identity].pop("reason", None)
    return list(entries.values())


# No parameters: return the selected pilot contracts without a second maintained ID list.
# Raises the original source/catalog validation error before any native operation.
def pilot_entries():
    return [entry for entry in load_catalog() if entry.get("suite") == "pilot"]


# identity: Qualified stable source identity or explicitly mapped inventory ID.
# Raises ValueError for unknown or ambiguous identities, without guessing enum values.
def entry_for(identity):
    matches = [entry for entry in load_catalog()
               if identity == entry["id"] or (entry["inventory_id"] is not None and identity == entry["inventory_id"])]
    if len(matches) != 1:
        raise ValueError(f"Unknown or ambiguous Studio screen identity: {identity!r}")
    return matches[0]


# identity: A qualified source identity or its mapped inventory ID.
# Returns the declared incoming and outgoing routes, without duplicate records.
# Raises the original catalog or identity error; this is not a navigation extraction.
def routes_for(identity):
    target = entry_for(identity)["id"]
    routes, seen = [], set()
    for entry in load_catalog():
        for route in entry.get("routes", []):
            if target not in (route.get("origin"), route.get("destination")):
                continue
            key = (route.get("origin"), route.get("destination"), route.get("event"),
                   route.get("source"), route.get("function"))
            if key not in seen:
                seen.add(key)
                routes.append(deepcopy(route))
    return routes


# entry: Validated catalog entry with a repository-relative source file.
# symbol: Optional current symbol; defaults to the entry's presenter symbol.
# Raises OSError or ValueError when source/symbol cannot be resolved in this checkout.
def resolve_source(entry, symbol=None):
    source = (ROOT / entry["source"]).resolve(strict=True)
    if not source.is_relative_to(ROOT):
        raise ValueError("Studio source escaped the repository")
    name = symbol or entry["symbol"]
    # Require a return type on the declaration line and a body after its
    # parameter list. Calls and forward declarations do not identify an owner.
    pattern = re.compile(r"^[ \t]*(?:(?:[\w:<>*&]+)[ \t]+)+" + re.escape(name) +
                         r"[ \t]*\([^;{}]*\)[ \t\r\n]*(?:const[ \t\r\n]*)?"
                         r"(?:noexcept[ \t\r\n]*)?\{", re.MULTILINE)
    content = source.read_text(encoding="utf-8")
    matches = list(pattern.finditer(content))
    if len(matches) != 1:
        raise ValueError(f"Source definition is missing or ambiguous in current checkout: {name}")
    return source, content.count("\n", 0, matches[0].start()) + 1
