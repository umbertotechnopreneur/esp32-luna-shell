"""Portable, daily Codex Standard catalog; only a qualitative band leaves the PC."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import date, timedelta
from decimal import Decimal
from enum import Enum
from html.parser import HTMLParser
import json
import math
import os
from pathlib import Path
import re
import sys
import threading
import time
from typing import Callable
from urllib.request import Request, urlopen


SOURCE = "https://learn.chatgpt.com/docs/pricing"
POLICY = "codex-standard-v1"
MAX_PAGE_BYTES = 1024 * 1024
MAX_CACHE_BYTES = 64 * 1024
MAX_DAY_MS = 26 * 60 * 60 * 1000  # A local day may include a daylight-saving transition.
HEADERS = ("Credits per 1M tokens", "Input Tokens", "Cached input tokens", "Output Tokens")
# Explicit catalog labels and exact hook IDs; unlisted models never inherit a similar name's rate.
MODEL_IDS = {
    "GPT-6 Astra": "gpt-6-astra", "GPT-6.1 Sol": "gpt-6.1-sol",
    "GPT-6 Sol": "gpt-6-sol", "GPT-6 Luna": "gpt-6-luna",
    "GPT-5.6 Sol": "gpt-5.6-sol", "GPT-5.6 Terra": "gpt-5.6-terra",
    "GPT-5.6 Luna": "gpt-5.6-luna", "GPT-Rosalind-Research": "gpt-rosalind-research",
    "GPT-5.5": "gpt-5.5",
}


class CostBand(str, Enum):
    UNKNOWN = "unknown"
    ECONOMY = "economy"
    BALANCED = "balanced"
    EXPENSIVE = "expensive"


@dataclass(frozen=True)
class PriceProjection:
    band: CostBand = CostBand.UNKNOWN
    age_ms: int | None = None
    valid_for_ms: int | None = None


# value: A bounded positive decimal credit rate, without currency or exponent notation.
# Raises ValueError for malformed, nonpositive or unreasonable source/cache rates.
def rate(value: str) -> Decimal:
    if not isinstance(value, str) or re.fullmatch(r"[0-9]{1,7}(\.[0-9]{1,6})?", value) is None:
        raise ValueError("Codex pricing rate is not a bounded decimal")
    result = Decimal(value)
    if not 0 < result <= 1000000:
        raise ValueError("Codex pricing rate is outside the supported positive range")
    return result


# input_rate: Official uncached input credits per million tokens at Standard speed.
# output_rate: Official output credits per million tokens at Standard speed.
# Raises no exceptions for validated rates; this product policy never estimates a chat bill.
def classify(input_rate: Decimal, output_rate: Decimal) -> CostBand:
    if input_rate <= 10 and output_rate <= 50:
        return CostBand.ECONOMY
    if input_rate <= 100 and output_rate <= 500:
        return CostBand.BALANCED
    return CostBand.EXPENSIVE


class CreditTableParser(HTMLParser):
    # self: Bounded public-document parser; only complete HTML tables are candidates.
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.tables: list[list[list[str]]] = []
        self.rows: list[list[str]] | None = None
        self.row: list[str] | None = None
        self.cell: list[str] | None = None

    # self: Parser retaining one bounded table at a time.
    # tag: Original HTML element identity.
    # attrs: HTML attributes are intentionally not interpreted as price data.
    # Raises ValueError for nested or oversized table structure.
    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag == "table":
            if self.rows is not None:
                raise ValueError("Codex pricing document has nested tables")
            self.rows = []
        elif self.rows is not None and tag == "tr":
            self.row = []
        elif self.row is not None and tag in {"th", "td"}:
            self.cell = []

    # self: Parser retaining bounded cell text without script or attribute inference.
    # data: Original decoded cell text.
    # Raises ValueError for an oversized cell.
    def handle_data(self, data: str) -> None:
        if self.cell is not None:
            self.cell.append(data)
            if sum(map(len, self.cell)) > 2048:
                raise ValueError("Codex pricing document cell exceeds its bound")

    # self: Parser committing only complete rows and tables.
    # tag: Original closing element identity.
    # Raises ValueError for excessive document tables or rows.
    def handle_endtag(self, tag: str) -> None:
        if tag in {"th", "td"} and self.cell is not None and self.row is not None:
            self.row.append(" ".join("".join(self.cell).split()))
            self.cell = None
        elif tag == "tr" and self.rows is not None and self.row is not None:
            self.rows.append(self.row)
            self.row = None
            if len(self.rows) > 128:
                raise ValueError("Codex pricing table exceeds its row bound")
        elif tag == "table" and self.rows is not None:
            self.tables.append(self.rows)
            self.rows = None
            if len(self.tables) > 64:
                raise ValueError("Codex pricing document exceeds its table bound")


# document: Bounded official HTML, including the Standard-rate qualification.
# Raises ValueError for absent/ambiguous tables, malformed recognized rows or duplicate models.
def parse_catalog(document: str) -> dict[str, tuple[Decimal, Decimal, Decimal]]:
    if len(document.encode("utf-8")) > MAX_PAGE_BYTES:
        raise ValueError("Codex pricing document exceeds its byte bound")
    parser = CreditTableParser()
    parser.feed(document)
    parser.close()
    tables = [rows for rows in parser.tables if rows and tuple(rows[0]) == HEADERS]
    if len(tables) != 1 or not any(
        "These are Standard credit rates." in cell for row in tables[0] for cell in row
    ):
        raise ValueError("Codex pricing requires one explicitly qualified Standard credit table")
    rates: dict[str, tuple[Decimal, Decimal, Decimal]] = {}
    for row in tables[0][1:]:
        if not row or row[0] not in MODEL_IDS:
            continue
        model = MODEL_IDS[row[0]]
        if len(row) != 4 or model in rates:
            raise ValueError(f"Codex pricing row is malformed or duplicated: {model}")
        parsed = []
        for cell in row[1:]:
            if re.fullmatch(r"[0-9]{1,3}(,[0-9]{3})*(\.[0-9]{1,6})? credits|[0-9]{1,7}(\.[0-9]{1,6})? credits", cell) is None:
                raise ValueError(f"Codex pricing credit rate is malformed: {model}")
            parsed.append(rate(cell.removesuffix(" credits").replace(",", "")))
        if parsed[1] > parsed[0]:
            raise ValueError(f"Codex cached input rate exceeds uncached input: {model}")
        rates[model] = tuple(parsed)
    if not rates:
        raise ValueError("Codex pricing table contains no supported exact model IDs")
    return rates


# No parameters: reads only the public official Codex document, never the API or chat contents.
# Raises the original network/HTTP/encoding error or ValueError for unsupported redirects/size.
def download_catalog() -> dict[str, tuple[Decimal, Decimal, Decimal]]:
    request = Request(SOURCE, headers={"User-Agent": "Cody-Codex-Pricing/0.6.4"})
    with urlopen(request, timeout=5) as response:
        if response.geturl() != SOURCE:
            raise ValueError("Codex pricing source redirected away from its configured document")
        document = response.read(MAX_PAGE_BYTES + 1)
    if len(document) > MAX_PAGE_BYTES:
        raise ValueError("Codex pricing download exceeds its byte bound")
    return parse_catalog(document.decode("utf-8"))


# timestamp: Actual PC wall time, also supplied explicitly by isolated host fixtures.
# Raises the original time conversion error for an unsupported PC clock value.
def local_day(timestamp: float) -> str:
    return time.strftime("%Y-%m-%d", time.localtime(timestamp))


# timestamp: Actual PC wall time in the current local calendar day.
# Raises the original local time conversion error; mktime preserves real DST midnight boundaries.
def remaining_day_ms(timestamp: float) -> int:
    tomorrow = date.fromisoformat(local_day(timestamp)) + timedelta(days=1)
    midnight = time.mktime((tomorrow.year, tomorrow.month, tomorrow.day, 0, 0, 0, 0, 0, -1))
    return int((midnight - timestamp) * 1000)


# pairs: One JSON object's original ordered fields.
# Raises ValueError for duplicate persisted keys rather than guessing which value owns the cache.
def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for name, value in pairs:
        if name in result:
            raise ValueError(f"Codex pricing cache has duplicate key {name!r}")
        result[name] = value
    return result


class PriceCache:
    # self: One existing exclusive broker's optional metadata owner; no platform-specific imports.
    # path: Non-secret cache under the broker's existing artifacts directory.
    # fetch: Bounded official download, replaceable only by isolated source fixtures.
    # wall_clock: Original PC wall clock; calendar freshness never follows a device heartbeat.
    def __init__(self, path: Path, fetch: Callable = download_catalog,
                 wall_clock: Callable[[], float] = time.time) -> None:
        self._path = path
        self._fetch = fetch
        self._clock = wall_clock
        self._lock = threading.Lock()
        self._worker: threading.Thread | None = None
        self._state: dict | None = None
        self._rates: dict[str, tuple[Decimal, Decimal, Decimal]] = {}
        self.error: str | None = None

    # self: Daily cache owner admitting a genuinely missing file as first startup only.
    # Raises original read/JSON/schema errors; invalid persistence is never repaired or overwritten.
    def _load(self) -> None:
        try:
            with self._path.open("rb") as source:
                raw = source.read(MAX_CACHE_BYTES + 1)
        except FileNotFoundError:
            self._state = {"schema": 1, "source": SOURCE, "policy": POLICY,
                           "attempted_day": None, "catalog": None, "error": None}
            print("CCP pricing: cache absent; first-start daily refresh admitted", file=sys.stderr, flush=True)
            return
        if len(raw) > MAX_CACHE_BYTES:
            raise ValueError("Codex pricing cache exceeds its byte bound")
        state = json.loads(raw, object_pairs_hook=unique_object)
        if (not isinstance(state, dict) or set(state) != {"schema", "source", "policy", "attempted_day", "catalog", "error"}
                or type(state["schema"]) is not int or state["schema"] != 1
                or state["source"] != SOURCE or state["policy"] != POLICY):
            raise ValueError("Codex pricing cache schema/source/policy is unsupported")
        if state["attempted_day"] is not None:
            if not isinstance(state["attempted_day"], str) or date.fromisoformat(state["attempted_day"]).isoformat() != state["attempted_day"]:
                raise ValueError("Codex pricing cache attempt day is malformed")
        if state["error"] is not None and (not isinstance(state["error"], str) or len(state["error"]) > 1024):
            raise ValueError("Codex pricing cache error is malformed")
        catalog = state["catalog"]
        rates = {}
        if catalog is not None:
            if (not isinstance(catalog, dict) or set(catalog) != {"fetched_at_ms", "day", "rates"}
                    or type(catalog["fetched_at_ms"]) is not int or catalog["fetched_at_ms"] <= 0
                    or not isinstance(catalog["day"], str) or date.fromisoformat(catalog["day"]).isoformat() != catalog["day"]
                    or not isinstance(catalog["rates"], dict) or not 1 <= len(catalog["rates"]) <= len(MODEL_IDS)):
                raise ValueError("Codex pricing cache catalog is malformed")
            for model, values in catalog["rates"].items():
                if model not in MODEL_IDS.values() or not isinstance(values, list) or len(values) != 3:
                    raise ValueError(f"Codex pricing cache model/rates are unsupported: {model}")
                parsed = tuple(rate(value) for value in values)
                if parsed[1] > parsed[0]:
                    raise ValueError(f"Codex pricing cache cached rate exceeds input: {model}")
                rates[model] = parsed
        self._state = state
        self._rates = rates
        self.error = state["error"]

    # self: Cache owner publishing one complete state without losing the previous valid catalog.
    # state: Validated daily attempt/catalog metadata; numeric rates stay on the PC.
    # Raises original filesystem/persistence errors; successful download alone is insufficient.
    def _persist(self, state: dict) -> None:
        self._path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self._path.with_suffix(".tmp")
        encoded = json.dumps(state, sort_keys=True, separators=(",", ":")).encode("utf-8")
        if len(encoded) > MAX_CACHE_BYTES:
            raise ValueError("Codex pricing cache publication exceeds its byte bound")
        with temporary.open("wb") as target:
            target.write(encoded)
            target.flush()
            os.fsync(target.fileno())
        temporary.replace(self._path)
        self._state = state

    # self: Exclusive broker starting the optional refresh once per local calendar day.
    # Raises original load, persistence or thread-start errors; callers report this optional failure.
    def start_daily_refresh(self) -> None:
        with self._lock:
            try:
                if self._state is None:
                    if self.error is not None:
                        raise RuntimeError(f"Codex pricing cache remains unavailable: {self.error}")
                    self._load()
                today = local_day(self._clock())
                if self._state["attempted_day"] == today or self._worker is not None and self._worker.is_alive():
                    return
                # Claim the day durably before network work, including failed downloads and restarts.
                self._persist({**self._state, "attempted_day": today, "error": "Refresh in progress"})
                self.error = "Refresh in progress"
                self._worker = threading.Thread(target=self._refresh, name="ccp-pricing", daemon=True)
                self._worker.start()
            except (OSError, ValueError, RuntimeError) as error:
                self.error = f"{type(error).__name__}: {error}"[:1024]
                raise

    # self: Background owner with a durably claimed day; it never blocks hook delivery.
    # Exceptions become explicit optional-metadata errors, preserving the last valid catalog on disk.
    def _refresh(self) -> None:
        try:
            rates = self._fetch()
            now = self._clock()
            catalog = {"fetched_at_ms": int(now * 1000), "day": local_day(now),
                       "rates": {model: [str(value) for value in values] for model, values in rates.items()}}
            with self._lock:
                self._persist({**self._state, "catalog": catalog, "error": None})
                self._rates = rates
                self.error = None
            print(f"CCP pricing: Standard catalog cached for {catalog['day']} ({len(rates)} exact model IDs)",
                  file=sys.stderr, flush=True)
        except Exception as error:
            with self._lock:
                self.error = f"{type(error).__name__}: {error}"[:1024]
                print(f"CCP pricing refresh failed: {self.error}", file=sys.stderr, flush=True)
                try:
                    self._persist({**self._state, "error": self.error})
                except (OSError, ValueError) as persistence_error:
                    print(f"CCP pricing failure persistence failed: {persistence_error}", file=sys.stderr, flush=True)

    # self: Memory-only catalog reader; network and filesystem work never run under an activity lock.
    # model: Exact genuinely observed hook ID; absence, aliases and unmatched names return Unknown.
    # Returns only a band and original source/expiry durations, never prices or estimated expenditure.
    def projection(self, model: str) -> PriceProjection:
        with self._lock:
            if self._state is None or self.error is not None or model not in self._rates:
                return PriceProjection()
            catalog = self._state["catalog"]
            now = self._clock()
            if not math.isfinite(now) or catalog["day"] != local_day(now):
                return PriceProjection()
            age_ms = int(now * 1000) - catalog["fetched_at_ms"]
            ttl_ms = remaining_day_ms(now)
            if not 0 <= age_ms <= MAX_DAY_MS or not 0 < ttl_ms <= MAX_DAY_MS:
                return PriceProjection()
            values = self._rates[model]
            return PriceProjection(classify(values[0], values[2]), age_ms, ttl_ms)

    # self: Existing broker's worker owner, stopped together with that broker.
    # Raises RuntimeError if the bounded download has not retired; shutdown is never silently claimed.
    def close(self) -> None:
        if self._worker is not None:
            self._worker.join(timeout=12)
            if self._worker.is_alive():
                raise RuntimeError("Codex pricing worker did not stop within its shutdown bound")
