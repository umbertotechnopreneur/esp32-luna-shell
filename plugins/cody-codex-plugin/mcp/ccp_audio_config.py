"""Persistent native audio configuration; credentials remain in their own file."""

from __future__ import annotations

from dataclasses import dataclass
import ipaddress
import json
import os
from pathlib import Path
import tempfile
from urllib.parse import urlsplit

from ccp_windows import runtime_directory


@dataclass(frozen=True)
class NativeAudioConfig:
    endpoint: str = ""
    token_file: str = ""
    source: str = "unconfigured"


# endpoint: Explicit loopback native host URL, never a credential-bearing URL.
# Raises ValueError for malformed, oversized, non-loopback or credential-bearing input.
def validate_endpoint(endpoint: str) -> str:
    if not isinstance(endpoint, str) or not 1 <= len(endpoint) <= 512:
        raise ValueError("Configure an explicit loopback Codex audio WebSocket endpoint")
    if not endpoint.isascii() or any(ord(character) <= 32 or ord(character) >= 127 for character in endpoint):
        raise ValueError("Codex audio endpoint must contain bounded printable ASCII without spaces")
    try:
        value = urlsplit(endpoint)
        valid = (value.scheme == "ws" and not value.username and not value.password
                 and value.hostname and value.port and not value.query and not value.fragment
                 and ipaddress.ip_address(value.hostname).is_loopback)
    except ValueError:
        valid = False
    if not valid:
        # Never echo rejected URLs: a mistake could contain credentials.
        raise ValueError("Codex audio endpoint must be a literal loopback WebSocket URL with an explicit port and no credentials")
    return endpoint


# token_file: Optional absolute path to an existing host capability; never its contents.
# Raises ValueError for a non-string, relative, oversized or control-bearing path.
def validate_token_file(token_file: str) -> str:
    if not isinstance(token_file, str) or len(token_file) > 1024 or any(ord(character) < 32 for character in token_file):
        raise ValueError("Codex audio token file path is invalid")
    if token_file and not Path(token_file).is_absolute():
        raise ValueError("Codex audio token file path must be absolute")
    return token_file


# pairs: JSON object members, preserving duplicate fields until validation.
# Raises ValueError rather than silently selecting one of two conflicting values.
def unique_fields(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Codex audio configuration contains duplicate fields")
        result[key] = value
    return result


# directory: Optional isolated storage owner for host fixtures; production uses local user storage.
# environment: Optional explicit environment; an endpoint override is intentional and takes precedence.
# Raises original read errors or ValueError for invalid persisted configuration; no repair or fallback.
def load_config(directory: Path | None = None, environment=None) -> NativeAudioConfig:
    environment = os.environ if environment is None else environment
    if "CODY_CODEX_AUDIO_ENDPOINT" in environment:
        return NativeAudioConfig(validate_endpoint(environment["CODY_CODEX_AUDIO_ENDPOINT"]),
                                 validate_token_file(environment.get("CODY_CODEX_AUDIO_TOKEN_FILE", "")), "environment")
    if environment.get("CODY_CODEX_AUDIO_TOKEN_FILE"):
        raise ValueError("Codex audio token file requires an explicit endpoint")
    path = (runtime_directory() if directory is None else directory) / "audio-config.json"
    try:
        with path.open("rb") as source:
            payload = source.read(4097)
    except FileNotFoundError:
        return NativeAudioConfig()
    if len(payload) > 4096:
        raise ValueError("Codex audio configuration exceeds its storage bound")
    try:
        value = json.loads(payload.decode("utf-8"), object_pairs_hook=unique_fields)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("Codex audio configuration is not valid UTF-8 JSON") from error
    if (not isinstance(value, dict) or set(value) != {"version", "endpoint", "token_file"}
            or type(value["version"]) is not int or value["version"] != 1):
        raise ValueError("Codex audio configuration schema is invalid")
    return NativeAudioConfig(validate_endpoint(value["endpoint"]), validate_token_file(value["token_file"]), "installation")


# configuration: Validated endpoint and token-file reference, after the installer verifies the native host.
# directory: Optional isolated storage owner for host fixtures.
# Raises original persistence errors; an invalid previous file is never silently replaced.
def save_config(configuration: NativeAudioConfig, directory: Path | None = None) -> Path:
    owner = runtime_directory() if directory is None else directory
    load_config(owner, environment={})
    value = {"version": 1, "endpoint": validate_endpoint(configuration.endpoint),
             "token_file": validate_token_file(configuration.token_file)}
    payload = json.dumps(value, ensure_ascii=True).encode("utf-8")
    if len(payload) > 4096:
        raise ValueError("Codex audio configuration exceeds its storage bound")
    owner.mkdir(parents=True, exist_ok=True)
    staging = owner / "artifacts" / "audio-setup"
    staging.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="wb", dir=staging, delete=False) as output:
            temporary = Path(output.name)
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        destination = owner / "audio-config.json"
        os.replace(temporary, destination)
    except BaseException as error:
        if temporary is not None:
            try:
                temporary.unlink(missing_ok=True)
            except OSError as cleanup_error:
                # Preserve the persistence cause even when its owned staging file cannot retire.
                error.add_note(f"Codex audio setup staging cleanup failed: {cleanup_error}")
        raise
    return destination
