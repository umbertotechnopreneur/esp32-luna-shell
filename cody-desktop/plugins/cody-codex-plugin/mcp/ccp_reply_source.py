"""Read only the exact completed Codex turn's authoritative final message."""

from __future__ import annotations

MAX_REPLY_BYTES = 400 * 64
_MAX_PAGES = 8
_PAGE_SIZE = 50


class CodexReplySource:
    # client: Already-running public App Server client, used only by its owner thread.
    # No native process or conversation is started or resumed for reply speech.
    def __init__(self, client, cancelled=None):
        self.client = client
        self.cancelled = cancelled

    def _check_cancelled(self):
        if self.cancelled is not None and self.cancelled.is_set():
            raise RuntimeError("Codex reply source request cancelled")

    # method: Supported read-only history operation.
    # params: Frozen exact thread/turn query; cursor is owned only by this operation.
    # Returns a complete bounded sequence of response entries.
    # Raises ValueError on malformed pages, repeated cursors or incomplete pagination.
    def _pages(self, method: str, params: dict) -> list[dict]:
        rows = []
        cursor = None
        seen = set()
        for _ in range(_MAX_PAGES):
            arguments = dict(params)
            if cursor is not None:
                arguments["cursor"] = cursor
            self._check_cancelled()
            result = self.client.call(method, arguments)
            if not isinstance(result, dict) or not isinstance(result.get("data"), list):
                raise ValueError("Codex reply source returned an invalid page")
            page = result["data"]
            if len(page) > _PAGE_SIZE or any(not isinstance(item, dict) for item in page):
                raise ValueError("Codex reply source returned an oversized page")
            rows.extend(page)
            cursor = result.get("nextCursor")
            if cursor is None:
                return rows
            if not isinstance(cursor, str) or not cursor or cursor in seen or len(cursor) > 4096:
                raise ValueError("Codex reply source returned an invalid cursor")
            seen.add(cursor)
        raise ValueError("Codex reply source exceeds its bounded pagination window")

    # thread_id: Exact genuine thread identity from the accepted native lifecycle hook.
    # turn_id: Exact genuine turn identity from that same hook.
    # Returns the single complete final-answer message as printable UTF-8 text.
    # Raises ValueError for unfinished, ambiguous, oversized or unsupported content.
    def final_answer(self, thread_id: str, turn_id: str) -> str:
        if not isinstance(thread_id, str) or not isinstance(turn_id, str) or not thread_id or not turn_id:
            raise ValueError("Codex reply requires an exact thread and turn")
        cursor = None
        seen = set()
        found = False
        for _ in range(_MAX_PAGES):
            params = {"threadId": thread_id, "limit": _PAGE_SIZE,
                      "sortDirection": "desc", "itemsView": "notLoaded"}
            if cursor is not None:
                params["cursor"] = cursor
            self._check_cancelled()
            page = self.client.call("thread/turns/list", params)
            if not isinstance(page, dict) or not isinstance(page.get("data"), list) or len(page["data"]) > _PAGE_SIZE:
                raise ValueError("Codex reply turn page is invalid")
            matches = [turn for turn in page["data"] if isinstance(turn, dict) and turn.get("id") == turn_id]
            if matches:
                if len(matches) != 1 or matches[0].get("status") != "completed":
                    raise ValueError("Codex reply turn is not uniquely completed")
                found = True
                break
            cursor = page.get("nextCursor")
            if cursor is None:
                break
            if not isinstance(cursor, str) or not cursor or cursor in seen or len(cursor) > 4096:
                raise ValueError("Codex reply turn cursor is invalid")
            seen.add(cursor)
        if not found:
            raise ValueError("Codex reply turn is not available in the bounded history window")
        entries = self._pages("thread/items/list", {
            "threadId": thread_id, "turnId": turn_id, "limit": _PAGE_SIZE, "sortDirection": "asc"})
        if any(entry.get("turnId") != turn_id or not isinstance(entry.get("item"), dict) for entry in entries):
            raise ValueError("Codex reply items do not match the originating turn")
        finals = [entry["item"] for entry in entries if entry["item"].get("type") == "agentMessage"
                  and entry["item"].get("phase") == "final_answer"]
        if len(finals) != 1:
            raise ValueError("Codex reply does not identify exactly one final answer")
        item = finals[0]
        text = item.get("text")
        identity = item.get("id")
        if not isinstance(identity, str) or not identity or not isinstance(text, str) or not text:
            raise ValueError("Codex final answer is missing its identity or text")
        # The device TTS accepts printable text. Normalize structural whitespace
        # without dropping words; unsupported controls are rejected below.
        text = text.replace("\r\n", " ").replace("\r", " ").replace("\n", " ").replace("\t", " ")
        encoded = text.encode("utf-8", "strict")
        if len(encoded) > MAX_REPLY_BYTES:
            raise ValueError("Codex final answer exceeds the reply speech limit")
        if any(ord(char) < 32 or 0x7f <= ord(char) <= 0x9f for char in text):
            # Preserve all content rather than silently normalizing control characters.
            raise ValueError("Codex final answer contains unsupported control characters")
        return text
