#!/usr/bin/env python3
"""Separate Linux process-tree RSS guard. No third-party monitoring dependency."""
import argparse
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import time

LIMIT_BYTES = 1_000_000_000
STOP_SIGNAL = getattr(signal, "SIGSTOP", 19)
KILL_SIGNAL = getattr(signal, "SIGKILL", 9)


def state_dir():
    return Path(os.environ.get("XDG_STATE_HOME", str(Path.home()/".local/state")))/"openai-usage"


def record(kind, message, memory=None):
    try:
        directory = state_dir()/"logs"
        directory.mkdir(parents=True, exist_ok=True, mode=0o700)
        path = directory/"linux.jsonl"
        if path.exists() and path.stat().st_size > 512*1024:
            path.replace(directory/"linux.jsonl.1")
        with path.open("a", encoding="utf-8") as output:
            output.write(json.dumps({"atUtc": time.time(), "kind": kind, "message": str(message)[:600], "bytes": memory})+"\n")
    except OSError:
        pass


def process_info(pid):
    try:
        line = Path(f"/proc/{pid}/stat").read_text()
        values = line[line.rfind(")")+2:].split()
        return {"pid": pid, "parent": int(values[1]), "state": values[0], "started": int(values[19]),
                "bytes": max(0, int(values[21]))*os.sysconf("SC_PAGE_SIZE")}
    except (OSError, ValueError, IndexError):
        return None


def descendants(root, snapshot, retained=(), excluded=()):
    result = {}
    pending = [root, *retained]
    excluded = set(excluded)
    while pending:
        pid = pending.pop()
        if pid in result or pid in excluded or pid not in snapshot:
            continue
        result[pid] = snapshot[pid]
        pending.extend(p for p, item in snapshot.items() if item["parent"] == pid)
    return result


def stop_owned(processes):
    # Verify Linux creation identity immediately before signalling, to reject PID reuse.
    for sig in (STOP_SIGNAL, KILL_SIGNAL):
        for pid, expected in processes.items():
            current = process_info(pid)
            if current and current["started"] == expected["started"]:
                try:
                    os.kill(pid, sig)
                except ProcessLookupError:
                    pass


def notice(message):
    try:
        import gi
        gi.require_version("Gtk", "4.0")
        from gi.repository import Gtk, GLib
        Gtk.init()
        dialog = Gtk.MessageDialog(modal=True, message_type=Gtk.MessageType.WARNING,
                                   buttons=Gtk.ButtonsType.OK, text="OpenAI Usage", secondary_text=message)
        loop = GLib.MainLoop()
        dialog.connect("response", lambda *_: loop.quit())
        dialog.present()
        loop.run()
        dialog.destroy()
    except Exception:
        print(message, file=sys.stderr)


def run(parent, identity, ready_fd):
    own = os.getpid()
    root = process_info(parent)
    if root is None or root["started"] != identity or os.getppid() != parent:
        return 2
    os.write(ready_fd, b"1")
    os.close(ready_fd)
    retained = {parent: root}
    last_heartbeat = 0
    try:
        while True:
            current = process_info(parent)
            if current is None or current["state"] == "Z" or current["started"] != identity:
                stop_owned({p: i for p, i in retained.items() if p != parent})
                return 0
            snapshot = {int(p.name): item for p in Path("/proc").iterdir() if p.name.isdigit()
                        and (item := process_info(int(p.name))) is not None}
            valid = [p for p, old in retained.items() if p in snapshot and snapshot[p]["started"] == old["started"]]
            retained = descendants(parent, snapshot, valid, [own])
            memory = sum(p["bytes"] for p in retained.values())
            if memory > LIMIT_BYTES:
                record("guard.memory-limit", "Widget and owned children stopped at the 1 GB aggregate RSS limit.", memory)
                stop_owned(retained)
                notice("OpenAI Usage exceeded its 1 GB memory limit.\nThe widget and its child processes have been stopped.\nDetails are in the local diagnostic logs.")
                return 137
            if time.monotonic()-last_heartbeat > 10:
                try:
                    state_dir().mkdir(parents=True, exist_ok=True, mode=0o700)
                    (state_dir()/"watchdog.json").write_text(json.dumps({"atUtc": time.time(), "pid": parent, "bytes": memory, "limitBytes": LIMIT_BYTES}))
                except OSError:
                    pass
                last_heartbeat = time.monotonic()
            time.sleep(.5)
    except Exception as error:
        record("guard.failure", type(error).__name__)
        stop_owned(retained)
        notice("Memory protection stopped working. OpenAI Usage has been closed. See the local diagnostic logs.")
        return 1


def start():
    if sys.platform != "linux":
        raise RuntimeError("The GTK watchdog requires Linux /proc.")
    parent = process_info(os.getpid())
    read_fd, write_fd = os.pipe()
    try:
        guard = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "--pid", str(os.getpid()),
                                  "--identity", str(parent["started"]), "--ready-fd", str(write_fd)],
                                 pass_fds=(write_fd,), start_new_session=True)
    finally:
        os.close(write_fd)
    try:
        if not select.select([read_fd], [], [], 10)[0] or os.read(read_fd, 1) != b"1":
            raise RuntimeError("The separate memory guard could not start.")
    finally:
        os.close(read_fd)
    record("guard.ready", "Separate 1 GB aggregate RSS watchdog active.")
    return guard


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--identity", type=int, required=True)
    parser.add_argument("--ready-fd", type=int, required=True)
    values = parser.parse_args()
    raise SystemExit(run(values.pid, values.identity, values.ready_fd))
