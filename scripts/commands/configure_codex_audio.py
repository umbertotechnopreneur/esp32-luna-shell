"""Installer step for an explicitly verified existing Desktop listener."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "plugins/cody-codex-plugin/mcp"))

import ccp_audio_config as configuration
import ccp_runtime as runtime
import ccp_voice_input as audio


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


# endpoint: Explicit listener already checked against Desktop process ownership by the public CLI.
# token_file: Absolute reference to the host capability, never a raw token.
# Raises original initialization/thread-read errors; submits no audio or other user input.
def verify_native_host(endpoint: str, token_file: str) -> None:
    thread_id = followed_chat()
    deadline = time.monotonic() + 5
    connection = audio.connect_native(endpoint, deadline, token_file)
    try:
        result = audio.rpc(connection, 2, "thread/read", {"threadId": thread_id, "includeTurns": False}, deadline)
        thread = result.get("thread", {})
        if thread.get("id") != thread_id or thread.get("status", {}).get("type") not in ("active", "idle"):
            # An independent host may find persisted history without owning the live chat.
            raise RuntimeError("The native endpoint does not own the currently followed chat")
        if followed_chat() != thread_id:
            raise RuntimeError("The followed Codex chat changed during audio setup; configuration was not changed")
    finally:
        connection.close()


# endpoint: User-supplied native URL; no server is created or chat resumed.
# token_file: Existing host capability-file reference, optionally empty.
# Raises original validation/probe/persistence errors; saves only after native reachability succeeds.
def configure(endpoint: str, token_file: str) -> Path:
    if "CODY_CODEX_AUDIO_ENDPOINT" in os.environ or "CODY_CODEX_AUDIO_TOKEN_FILE" in os.environ:
        raise ValueError("Remove the explicit Cody audio environment override before saving installation configuration")
    selected = configuration.NativeAudioConfig(configuration.validate_endpoint(endpoint),
                                              configuration.validate_token_file(token_file), "installation")
    # Corrupt persisted state requires explicit repair; installation never hides it.
    configuration.load_config(environment={})
    verify_native_host(selected.endpoint, selected.token_file)
    return configuration.save_config(selected)


# No parameters: consumes bounded installer options; prints no tokens, chat contents or private IDs.
# Returns nonzero on setup failure; a successful read probe is not proof of dictation/model support.
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--token-file", default="")
    arguments = parser.parse_args()
    try:
        configure(arguments.endpoint, arguments.token_file)
    except Exception as error:
        print(f"CCP audio setup failed: {error}", file=sys.stderr)
        return 1
    print("CCP native audio configuration saved; read-only live-chat probe passed.")
    print("Restart Codex to load the installed plugin configuration, then send a genuine prompt.")
    print("Check ccp_audio_status.native_ready before dictation; model/audio acceptance remains unverified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
