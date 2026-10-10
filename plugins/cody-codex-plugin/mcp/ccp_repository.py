"""Read bounded Git checkout metadata from a genuine hook cwd, without commands or transcripts."""

from pathlib import Path
import re
import sys


# path: Exact Git administrative file; only HEAD and an optional worktree gitdir pointer are admitted.
# maximum: Explicit byte limit before decoding metadata.
# Raises OSError or ValueError for genuine read, size or ASCII encoding failures.
def read_metadata(path: Path, maximum: int) -> str:
    with path.open("rb") as source:
        raw = source.read(maximum + 1)
    if len(raw) > maximum:
        raise ValueError("Git metadata exceeds its explicit byte bound")
    return raw.decode("ascii")


# cwd: Genuine hook working directory; no prompt, tool argument or guessed checkout is inspected.
# Returns the exact bounded branch, detached state, or empty unavailable metadata with the real error logged.
def read_branch(cwd: str) -> str:
    try:
        directory = Path(cwd)
        for depth, candidate in enumerate((directory, *directory.parents)):
            if depth >= 64:
                raise ValueError("Git checkout ancestry exceeds its explicit 64-directory bound")
            administrative = candidate / ".git"
            try:
                administrative.stat()
            except FileNotFoundError:
                continue
            if administrative.is_file():
                pointer = read_metadata(administrative, 4096).rstrip("\r\n")
                if not pointer.startswith("gitdir: ") or "\n" in pointer or "\r" in pointer:
                    raise ValueError("Git worktree pointer is malformed")
                target = Path(pointer[8:])
                administrative = target if target.is_absolute() else candidate / target
            head = read_metadata(administrative / "HEAD", 512).rstrip("\r\n")
            if head.startswith("ref: refs/heads/"):
                branch = head[len("ref: refs/heads/"):]
                if not 1 <= len(branch) <= 64 or any(not 33 <= ord(character) <= 126 for character in branch):
                    raise ValueError("Git branch is not representable as 1-64 printable ASCII characters")
                return branch
            if re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", head):
                return "DETACHED"
            raise ValueError("Git HEAD does not identify a branch or detached commit")
        return ""
    except (OSError, ValueError, UnicodeDecodeError) as error:
        print(f"CCP repository branch read unavailable: {error}", file=sys.stderr, flush=True)
        return ""
