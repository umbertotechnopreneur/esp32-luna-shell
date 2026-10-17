"""Inspect Codex hook parsing with isolated packages; never execute a hook."""

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import queue
import subprocess
import threading


# Parameter executable: the native Codex executable to inspect.
# Parameter catalog: an isolated marketplace catalog under artifacts.
# Parameter names: package names declared by that catalog.
# Parameter include_runtime: also inspect discovered hooks without executing them.
# Exceptions: propagate subprocess, timeout and native API errors.
def inspect_packages(executable, catalog, names, include_runtime=False):
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
        runtime = []
        if include_runtime:
            listing = request({"id": 100, "method": "hooks/list", "params": {}})
            for entry in listing["data"]:
                hooks = entry["hooks"]
                fields = ("event", "eventName", "event_name", "enabled", "trusted",
                          "isTrusted", "approvalStatus", "status", "trustStatus",
                          "pluginId", "source", "sourcePath", "statusMessage")
                runtime.append({
                    "cwd": entry["cwd"],
                    "hook_count": len(hooks),
                    "hooks": [{key: hook[key] for key in fields if key in hook} for hook in hooks],
                    "metadata_fields": sorted({key for hook in hooks for key in hook}),
                    "warnings": entry.get("warnings", []),
                    "errors": entry.get("errors", []),
                })
        return results, runtime
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
    legacy_manifest = json.loads(
        (source / ".codex-plugin" / "plugin.json").read_text(encoding="utf-8"))
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
            if manifest_style == "legacy":
                manifest = dict(legacy_manifest)
                manifest["name"] = name
                manifest["version"] = "0.0.1"
            elif manifest_style in ("portable", "dual"):
                # Preserve the failing portable representation as a diagnostic fixture,
                # independently of the canonical production manifest.
                for field in ("description", "author"):
                    manifest[field] = legacy_manifest[field]
                manifest["extensions"] = {"com.openai": {
                    "interface": legacy_manifest["interface"],
                    "hooks": legacy_manifest["hooks"],
                }}
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
    parser.add_argument("--installed", action="store_true",
                        help="Inspect installed CCP and the runtime hook list; do not execute hooks.")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = root / "artifacts" / "ccp-hook-discovery-20261010" / "parser-probes"
    if arguments.installed:
        output.mkdir(parents=True, exist_ok=True)
        catalog = root / ".agents" / "plugins" / "marketplace.json"
        names = ["cody-codex-plugin"]
    else:
        catalog, names = prepare_packages(output)
    report = []
    for executable in arguments.codex_executable:
        version = subprocess.check_output([str(executable), "--version"], text=True).strip()
        cases, runtime = inspect_packages(executable, catalog, names, arguments.installed)
        report.append({"executable": str(executable), "version": version,
                       "cases": cases, "runtime": runtime})
        print(version)
        for case in cases:
            print(f"  {case['case']}: {case['hook_count']} hooks")
            if case["errors"]:
                print(f"    Errors: {case['errors']}")
        for entry in runtime:
            summaries = [{key: hook[key] for key in ("eventName", "statusMessage", "enabled", "trustStatus")
                          if key in hook} for hook in entry["hooks"]]
            print(f"  Installed runtime: {entry['hook_count']} hooks; metadata: {summaries}")
    filename = "installed-report.json" if arguments.installed else "report.json"
    (output / filename).write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Report: {output / filename}")
    if any(case["hook_count"] is None for item in report for case in item["cases"]):
        raise RuntimeError("Some isolated packages could not be inspected; see the report.")
    if arguments.installed:
        authored = json.loads((root / "plugins/cody-codex-plugin/hooks/hooks.json").read_text(
            encoding="utf-8"))["hooks"]
        # Native discovery lowercases the initial event character. Keep labels
        # in the identity so a duplicate cannot substitute for a missing hook.
        expected = Counter((event[0].lower() + event[1:], hook["statusMessage"])
                           for event, groups in authored.items()
                           for group in groups for hook in group["hooks"])
        if not expected or any(count != 1 for count in expected.values()):
            raise RuntimeError("Authored CCP hooks have empty or duplicate identities.")
        for item in report:
            if item["cases"][0]["errors"] or item["cases"][0]["hook_count"] != sum(expected.values()):
                raise RuntimeError("Installed CCP did not expose all authored hooks without errors.")
            discovered = Counter((hook.get("eventName"), hook.get("statusMessage"))
                                 for entry in item["runtime"] for hook in entry["hooks"]
                                 if hook.get("pluginId") == "cody-codex-plugin@cody-local")
            if discovered != expected:
                raise RuntimeError("Runtime CCP hook identities differ from authored events/labels: "
                                   f"missing={dict(expected - discovered)}, "
                                   f"unexpected={dict(discovered - expected)}")
            if any(entry["errors"] for entry in item["runtime"]):
                raise RuntimeError("The runtime reported a hook discovery error; see the report.")
            if any(not (hook.get("statusMessage") or "").strip()
                   for entry in item["runtime"] for hook in entry["hooks"]):
                raise RuntimeError("An installed hook has no readable review label.")


if __name__ == "__main__":
    main()
