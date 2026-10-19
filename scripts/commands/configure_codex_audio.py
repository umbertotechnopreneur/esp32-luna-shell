"""Opt-in WAV queue setup; checks existing chat metadata without submitting input."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "plugins/cody-codex-plugin/mcp"))

import ccp_audio_config as configuration
import ccp_runtime as runtime
from ccp_audio_queue import PublicAudioQueue


# No parameters: reads genuine hook metadata from the existing authenticated broker.
# Raises original transport errors or RuntimeError for missing/ambiguous followed context.
def followed_chat() -> str:
    # Do not acquire an MCP lease or launch a broker for this installation probe.
    result = runtime.exchange("mcp", {"id": 1, "method": "tools/call",
        "params": {"name": "ccp_list_sessions", "arguments": {}}}, runtime.control_secret())
    if "error" in result or result.get("result", {}).get("isError"):
        raise RuntimeError("CCP could not provide genuine followed chat metadata")
    sessions = result.get("result", {}).get("structuredContent", {}).get("sessions", [])
    followed = [item for item in sessions if item.get("followed") is True]
    if len(followed) != 1 or not followed[0].get("session_id"):
        raise RuntimeError("Send a genuine prompt in the intended Codex chat before audio setup")
    return followed[0]["session_id"]


# executable: Installed native CLI selected by the public setup command.
# Raises original protocol errors or a changed followed identity; sends no user input.
def verify_native_host(executable: str) -> None:
    thread_id = followed_chat()
    client = PublicAudioQueue(executable)
    try:
        client.probe(thread_id)
        if followed_chat() != thread_id:
            raise RuntimeError("The followed Codex chat changed during audio setup; configuration was not changed")
    finally:
        client.close()


# executable: User-selected installed native CLI; never an endpoint or API key.
# Raises original validation/probe/persistence errors; saves only after public queue reads.
def configure(executable: str) -> Path:
    overrides = ("CODY_CODEX_AUDIO_EXECUTABLE", "CODY_CODEX_AUDIO_ENDPOINT", "CODY_CODEX_AUDIO_TOKEN_FILE")
    if any(key in os.environ for key in overrides):
        raise ValueError("Remove explicit Cody audio environment settings before saving installation configuration")
    selected = configuration.NativeAudioConfig(configuration.validate_executable(executable), "installation")
    configuration.load_config(environment={})
    verify_native_host(selected.executable)
    return configuration.save_config(selected)


# No parameters: explicit installer invocation; no audio, transcripts or credentials are sent.
# Returns nonzero on setup failure; readiness proves metadata/queue access only.
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--codex-executable", required=True)
    arguments = parser.parse_args()
    try:
        configure(arguments.codex_executable)
    except Exception as error:
        print(f"CCP audio queue setup failed: {error}", file=sys.stderr)
        return 1
    print("CCP public audio queue configuration saved; read-only chat/queue probe passed.")
    print("Reload the installed CCP broker to adopt configuration, then send a genuine prompt.")
    print("WAV delivery only: queue admission does not prove transcription or model understanding.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
