"""Inspect Codex hook parsing with isolated packages; never execute a hook."""

import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import threading


# Parameter executable: the native Codex executable to inspect.
# Parameter catalog: an isolated marketplace catalog under artifacts.
# Parameter names: package names declared by that catalog.
# Exceptions: propagate subprocess, timeout and native API errors.
def inspect_packages(executable, catalog, names):
    process = subprocess.Popen(
        [str(executable), "app-server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        encoding="utf-8",
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
    )
    responses = queue.Queue()

    # Exceptions: malformed native response JSON is reported to the caller.
    def read_responses():
        try:
            for line in process.stdout:
                responses.put(json.loads(line))
        except Exception as error:
            responses.put(error)

    threading.Thread(target=read_responses, daemon=True).start()

    # Parameter message: one native API request or lifecycle notification.
    # Exceptions: propagate broken pipes, response timeout and native API errors.
    def request(message):
        process.stdin.write(json.dumps(message) + "\n")
        process.stdin.flush()
        if "id" not in message:
            return None
        while True:
            response = responses.get(timeout=15)
            if isinstance(response, Exception):
                raise response
            if response.get("id") != message["id"]:
                continue
            if "error" in response:
                raise RuntimeError(response["error"])
            return response["result"]

    try:
        request({"id": 1, "method": "initialize", "params": {
            "clientInfo": {"name": "ccp_hook_discovery", "version": "1"},
            "capabilities": {"experimentalApi": True},
        }})
        request({"method": "initialized"})
        results = []
        for index, name in enumerate(names, start=2):
            try:
                result = request({"id": index, "method": "plugin/read", "params": {
                    "pluginName": name,
                    "marketplacePath": str(catalog),
                }})
            except RuntimeError as error:
                results.append({"case": name, "hook_count": None, "errors": [str(error)],
                                "warnings": []})
                continue
            # Only report package metadata, never hook commands or user configuration.
            plugin = result.get("plugin", result)
            if "hooks" not in plugin:
                raise RuntimeError(f"Native plugin response has no hooks field: {list(plugin)}")
            results.append({"case": name, "hook_count": len(plugin["hooks"]),
                            "errors": plugin.get("errors", []),
                            "warnings": plugin.get("warnings", [])})
        return results
    finally:
        process.stdin.close()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=5)


# Parameter output: the artifact directory owning every isolated package.
# Exceptions: propagate filesystem errors rather than hide incomplete fixtures.
def prepare_packages(output):
    source = Path(__file__).resolve().parents[2] / "plugins" / "cody-codex-plugin"
    authored_hooks = json.loads((source / "hooks" / "hooks.json").read_text(encoding="utf-8"))
    handler = {"type": "command", "command": "python -c \"pass\"", "timeout": 8}
    group = [{"hooks": [handler]}]
    formats = {
        "pascal-wrapped": authored_hooks,
        "pascal-direct": {"SessionStart": group},
        "snake-wrapped": {"hooks": {"session_start": group}},
        "snake-direct": {"session_start": group},
    }
    entries = []
    for manifest_style in ("portable", "legacy", "portable-top-level", "portable-default", "dual"):
        for format_name, hooks in formats.items():
            name = f"ccp-probe-{manifest_style}-{format_name}"
            package = output / "plugins" / name
            (package / "hooks").mkdir(parents=True, exist_ok=True)
            (package / "hooks" / "hooks.json").write_text(
                json.dumps(hooks, indent=2), encoding="utf-8")
            manifest = {"name": name, "version": "0.0.1", "description": "Hook parser inspection"}
            if manifest_style in ("portable", "legacy", "dual"):
                original = source / "plugin.json"
                if manifest_style == "legacy":
                    original = source / ".codex-plugin" / "plugin.json"
                manifest = json.loads(original.read_text(encoding="utf-8"))
                manifest["name"] = name
                manifest["version"] = "0.0.1"
            if manifest_style != "legacy":
                manifest["$schema"] = "https://agent-plugins.org/schemas/1.0.0/plugin.schema.json"
                if manifest_style in ("portable", "dual"):
                    extension = manifest.setdefault("extensions", {}).setdefault("com.openai", {})
                    extension["hooks"] = "./hooks/hooks.json"
                elif manifest_style == "portable-top-level":
                    manifest["hooks"] = "./hooks/hooks.json"
                target = package / "plugin.json"
            else:
                manifest["hooks"] = "./hooks/hooks.json"
                target = package / ".codex-plugin" / "plugin.json"
                target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
            if manifest_style == "dual":
                overlay = package / ".codex-plugin" / "plugin.json"
                overlay.parent.mkdir(parents=True, exist_ok=True)
                overlay_manifest = json.loads(
                    (source / ".codex-plugin" / "plugin.json").read_text(encoding="utf-8"))
                overlay_manifest["name"] = name
                overlay_manifest["version"] = "0.0.1"
                overlay.write_text(json.dumps(overlay_manifest, indent=2), encoding="utf-8")
            # Catalog sources stay relative to this isolated marketplace root.
            source_path = f"./plugins/{name}"
            entries.append({"name": name, "source": {
                "source": "local", "path": source_path}})
    catalog = output / ".agents" / "plugins" / "marketplace.json"
    catalog.parent.mkdir(parents=True, exist_ok=True)
    catalog.write_text(json.dumps({"name": "ccp-hook-inspection", "plugins": entries},
                                  indent=2), encoding="utf-8")
    return catalog, [entry["name"] for entry in entries]


# Exceptions: propagate discovery failures and report an unsuccessful inspection.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--codex-executable", type=Path, action="append", required=True)
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = root / "artifacts" / "ccp-hook-discovery-20261010" / "parser-probes"
    catalog, names = prepare_packages(output)
    report = []
    for executable in arguments.codex_executable:
        version = subprocess.check_output([str(executable), "--version"], text=True).strip()
        cases = inspect_packages(executable, catalog, names)
        report.append({"executable": str(executable), "version": version, "cases": cases})
        print(version)
        for case in cases:
            print(f"  {case['case']}: {case['hook_count']} hooks")
            if case["errors"]:
                print(f"    Errors: {case['errors']}")
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Report: {output / 'report.json'}")
    if any(case["hook_count"] is None for item in report for case in item["cases"]):
        raise RuntimeError("Some isolated packages could not be inspected; see the report.")


if __name__ == "__main__":
    main()
