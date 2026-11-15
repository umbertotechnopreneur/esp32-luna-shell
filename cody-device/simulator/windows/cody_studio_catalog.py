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


# identity: Qualified source key, never a firmware enum ordinal.
# state: Availability prose maintained in the source catalog.
# owner: Presenter/dependency description from the same source row.
# Returns the declared applicability and a specific reason when no executable contract exists.
# Raises no exceptions; this projection never marks a source-only row capturable.
def source_availability(identity, state, owner):
    if state.lower().startswith(("retained", "retired", "planned", "reference only")):
        return "not-applicable", f"Outside the current native screen suite: {state}"
    if identity.startswith("widget-v1:"):
        reason = "Counter-specific account, history or device observations have no direct native fixture in this batch"
    elif identity.startswith("ui-v1:wifi-"):
        reason = "The local Wi-Fi owner needs a scan/result or credential-entry fixture before direct preparation"
    elif identity in ("atlas-v1:07", "atlas-v1:12", "atlas-v1:13", "atlas-v1:14", "atlas-v1:15",
                      "ui-v1:retained-pc", "ui-v1:forget-pc", "ui-v1:pairing-cleanup", "ui-v1:pairing-forgotten"):
        reason = "The pairing/discovery owner needs a retained association and a scripted operation lifecycle"
    elif identity in ("atlas-v1:28", "atlas-v1:29"):
        reason = "Voice capture/delivery needs a profile-supported session, frozen destination and transport observations"
    elif identity in ("atlas-v1:35A", "atlas-v1:35B"):
        reason = "Sleep/wake requires the power owner's lifecycle and retained display context; no static fixture is declared"
    elif identity.startswith("ui-v1:restart-") or identity.startswith("ui-v1:reset-"):
        reason = "The maintenance operation/result fixture is not implemented; no restart or reset is performed for the atlas"
    elif identity in ("ui-v1:diagnostics", "ui-v1:memory-detail"):
        reason = "The diagnostics owner has no direct native fixture for this view and its observed metrics"
    elif identity == "ui-v1:screensaver":
        reason = "Screensaver admission and return to a retained route have no isolated native fixture"
    elif identity.startswith("next-v1:"):
        reason = "The current domain/controller entry has no direct native preparation contract"
    elif identity.startswith("ui-v1:") or identity.startswith("atlas-v1:"):
        reason = "The route's event, selection or operation dependencies have no isolated native fixture in this batch"
    else:
        reason = "No isolated native preparation contract is declared for this source"
    return "unsupported", f"{reason}. Source owner: {owner}"


# identity: Qualified symbolic key from a compact source table.
# name: Symbolic row value before namespace qualification.
# cells: Maintained source row, including its owner and state.
# Returns a host catalog label; device captions continue to come from C/C++ renderers.
# Raises no exceptions; labels do not introduce a new maintained screen list.
def source_title(identity, name, cells):
    if len(cells) >= 5 or identity.startswith("next-v1:") and identity not in ("next-v1:10", "next-v1:16"):
        return cells[1]
    symbolic = name.split("=", 1)[0]
    if " / " in cells[0]:
        symbolic = cells[0].split(" / ", 1)[1]
    symbolic = re.sub(r"(?<=[a-z])(?=[A-Z])", " ", symbolic).replace("-", " ")
    return symbolic.title()


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
            availability, reason = source_availability(identity, cells[-1], cells[1] if len(cells) == 4 else cells[2])
            entries.append({"id": identity, "title": source_title(identity, name, cells), "source_state": cells[-1],
                            "source_owner": cells[1] if len(cells) == 4 else cells[2],
                            "availability": availability, "inventory_id": None, "inventory_family_id": None,
                            "reason": reason, "variants": [],
                            "routes": []})
    if not entries:
        raise ValueError("Source catalog contains no qualified screen identities")
    return entries


# No parameters: project source identities plus their explicit executable contracts.
# Raises OSError, JSON errors or ValueError for invalid extensions, variants or family references.
def load_catalog():
    extension = json.loads(EXTENSION.read_text(encoding="utf-8"))
    if extension.get("schema") != "cody.studio.catalog.v1":
        raise ValueError("Unsupported Studio catalog schema")
    entries = {entry["id"]: entry for entry in source_entries()}
    declared = set()
    inventory_ids = set()
    family_references = []
    for contract in extension["entries"]:
        identity = contract["id"]
        inventory_id = contract.get("inventory_id")
        family_id = contract.get("inventory_family_id")
        if inventory_id is not None and (not isinstance(inventory_id, str) or not inventory_id):
            raise ValueError(f"Invalid primary inventory identity: {identity}")
        if family_id is not None:
            if inventory_id is not None or not isinstance(family_id, str) or not family_id:
                raise ValueError(f"An inventory family requires a nonempty reference and no primary ID: {identity}")
            family_references.append((identity, family_id))
        if identity not in entries or identity in declared or (inventory_id and inventory_id in inventory_ids):
            raise ValueError(f"Unknown or duplicate Studio catalog contract: {identity}")
        if contract.get("availability") not in AVAILABILITIES:
            raise ValueError(f"Invalid Studio availability: {identity}")
        if (contract.get("availability") in RENDERING_MODES and
                contract.get("rendering_mode") != contract["availability"]):
            raise ValueError(f"Studio rendering mode and availability disagree: {identity}")
        variant_files = contract.get("variant_scenarios", {})
        variant_contracts = contract.get("variant_contracts", {})
        variants = contract.get("variants", [])
        if (not isinstance(variants, list) or any(not isinstance(variant, str) or
                not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", variant) for variant in variants) or
                len(variants) != len(set(variants))):
            raise ValueError(f"Invalid or duplicate Studio variants: {identity}")
        if (not isinstance(variant_files, dict) or not isinstance(variant_contracts, dict) or
                set(variant_files) != set(variant_contracts)):
            raise ValueError(f"Studio variant fixtures and contracts disagree: {identity}")
        for variant, filename in variant_files.items():
            if (variant == "default" or variant not in variants or not isinstance(filename, str) or
                    not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*\.json", filename) or
                    not isinstance(variant_contracts[variant], dict)):
                raise ValueError(f"Invalid Studio variant fixture or contract: {identity}/{variant}")
        if contract.get("atlas_variant") is not None and contract["atlas_variant"] not in variants:
            raise ValueError(f"Unknown Studio atlas variant: {identity}")
        declared.add(identity)
        inventory_ids.add(inventory_id)
        entries[identity].update(deepcopy(contract))
        entries[identity]["inventory"] = deepcopy(extension["inventory"])
        if contract["availability"] in RENDERING_MODES or contract["availability"] == "profile-conditional":
            entries[identity].pop("reason", None)
    for identity, family_id in family_references:
        if family_id not in inventory_ids:
            raise ValueError(f"Unknown primary inventory family {family_id!r} for {identity}")
    return list(entries.values())


# entry: Validated catalog entry retaining its original default schema and explicit variant overrides.
# variant: Declared scenario variant; changing query availability does not create a new screen identity.
# Returns a detached strict contract without modifying the default or a historical capture.
# Raises ValueError for an unknown variant or a destination without an executable contract.
def scenario_contract_for(entry, variant):
    if variant not in entry.get("variants", []):
        raise ValueError(f"Unknown Studio scenario variant: {entry['id']}/{variant}")
    contract = entry.get("variant_contracts", {}).get(variant, entry.get("scenario_contract"))
    if not isinstance(contract, dict):
        raise ValueError(f"Studio scenario contract is unavailable: {entry['id']}/{variant}")
    return deepcopy(contract)


# No parameters: return the selected pilot contracts without a second maintained ID list.
# Raises the original source/catalog validation error before any native operation.
def pilot_entries():
    return [entry for entry in load_catalog() if entry.get("suite") == "pilot"]


# identity: Qualified stable source identity or explicitly mapped primary inventory ID.
# Secondary family members retain their qualified source lookup, avoiding ambiguous inventory aliases.
# Raises ValueError for unknown or ambiguous identities, without guessing enum values.
def entry_for(identity):
    matches = [entry for entry in load_catalog()
               if identity == entry["id"] or (entry["inventory_id"] is not None and identity == entry["inventory_id"])]
    if len(matches) != 1:
        raise ValueError(f"Unknown or ambiguous Studio screen identity: {identity!r}")
    return matches[0]


# item: Catalog entry or capture metadata with an optional primary or family inventory ID.
# Returns an unambiguous display label; secondary members keep their source lookup identity.
# Raises KeyError only when an unmapped item does not contain its qualified source ID.
def inventory_label(item):
    if item.get("inventory_id"):
        return item["inventory_id"]
    if item.get("inventory_family_id"):
        return f"{item['inventory_family_id']} (family)"
    return item["id"]


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
