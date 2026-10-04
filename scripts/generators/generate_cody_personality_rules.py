"""Compile the single-value personality YAML into immutable firmware rules."""

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from scripts.common.asset_provenance import text_source_hash
from scripts.common.cody_personality_contract import load_personality_rules


# rules: Strictly validated canonical authoring rules.
def generated_header(rules):
    happiness = rules["happiness"]
    values = (happiness["minimum"], happiness["maximum"], happiness["initial"],
              happiness["on_caress"]["add"], happiness["decay"]["subtract"],
              happiness["decay"]["every_seconds"] * 1_000_000)
    encoded = ", ".join(str(value) for value in values)
    return "\n".join((
        "// Generated from assets/cody/personality.yaml; edit that source, then regenerate.",
        "#pragma once", '#include "CODY_happiness.hpp"', "namespace CODY::PersonalityData {",
        f"inline constexpr HappinessConfig happiness{{{encoded}}};",
        "} // namespace CODY::PersonalityData", "",
    ))


# argv: Explicit CLI arguments forwarded by scripts/device.ps1 generate.
# Raises the original authoring or filesystem error; no default/repair is attempted.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    source = ROOT / "assets/cody/personality.yaml"
    rules = load_personality_rules(source)
    output = ROOT / "assets/cody/personality/generated"
    output.mkdir(parents=True, exist_ok=True)
    header = output / "personality_rules.hpp"
    header.write_text(generated_header(rules), encoding="utf-8")
    manifest = {"schema": "cody.personality.generated.v1",
                "source": source.relative_to(ROOT).as_posix(),
                "source_sha256": text_source_hash(source),
                "header": header.relative_to(ROOT).as_posix(),
                "rules": rules}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated session happiness rules: {header}")


if __name__ == "__main__":
    main()
