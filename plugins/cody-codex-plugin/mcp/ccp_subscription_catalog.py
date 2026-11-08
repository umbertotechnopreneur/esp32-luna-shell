"""Public US ChatGPT subscription presentation for the paired Cody account page."""

from __future__ import annotations

from datetime import datetime, timezone
from hashlib import blake2s
import re
import sys
import threading
import time
from urllib.request import Request, urlopen


SOURCE = "https://learn.chatgpt.com/docs/pricing.md"
MAX_DOCUMENT_BYTES = 128 * 1024
REFRESH_SECONDS = 24 * 60 * 60
FRESH_SECONDS = 2 * REFRESH_SECONDS
# Published prices verified on 2026-11-08. Bound the offline bootstrap separately.
SEED_VALID_UNTIL = datetime(2026, 12, 8, tzinfo=timezone.utc).timestamp()
SEED_PRICES = {
    "free": "0 USD/mo",
    "go": "8 USD/mo",
    "plus": "20 USD/mo",
    "pro": "100/200/500 USD/mo",
    "business": "25 USD/user/mo",
}

PLAN_ALIASES = {
    "prolite": "pro", "promax": "pro", "team": "business", "ent26": "enterprise",
    "self_serve_business_prolite": "business", "self_serve_business_usage_based": "business",
    "enterprise_cbp_automation": "enterprise", "enterprise_cbp_usage_based": "enterprise",
    "edu_plus": "edu", "edu_pro": "edu",
}
CONTRACT_PLANS = frozenset({
    "self_serve_business_prolite", "self_serve_business_usage_based",
    "enterprise_cbp_automation", "enterprise_cbp_usage_based",
    "edu_plus", "edu_pro",
})

# Each word is one 16-pixel row, with the left pixel in the most significant bit.
PLAN_GLYPHS = {
    "free": (0, 0, 0x03c0, 0x0ff0, 0x1c38, 0x1818, 0x300c, 0x300c,
             0x300c, 0x300c, 0x1818, 0x1c38, 0x0ff0, 0x03c0, 0, 0),
    "go": (0, 0, 0x0180, 0x00c0, 0x0060, 0x0030, 0x0018, 0x3ffc,
           0x3ffc, 0x0018, 0x0030, 0x0060, 0x00c0, 0x0180, 0, 0),
    "plus": (0, 0, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x3ffc,
             0x3ffc, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0, 0),
    "pro": (0, 0, 0x0ff0, 0x1ff8, 0x324c, 0x6246, 0x7ffe, 0x366c,
            0x1668, 0x0e70, 0x0770, 0x03c0, 0x0180, 0, 0, 0),
    "business": (0, 0, 0x07e0, 0x0660, 0x0660, 0x3ffc, 0x7ffe, 0x6006,
                 0x7ffe, 0x6186, 0x6006, 0x6006, 0x7ffe, 0x3ffc, 0, 0),
    "enterprise": (0, 0, 0x0ff0, 0x0ff0, 0x0810, 0x0db0, 0x0db0, 0x0810,
                   0x0db0, 0x0db0, 0x0810, 0x0990, 0x0990, 0x3ffc, 0x3ffc, 0),
    "edu": (0, 0, 0x0180, 0x07e0, 0x1c38, 0x700e, 0x6006, 0x783e,
            0x1e7a, 0x13ca, 0x100a, 0x183a, 0x0ff2, 0x07e0, 0, 0),
}
PLAN_LABELS = {key: f"ChatGPT {key.title()}" for key in PLAN_GLYPHS}
PLAN_TOKEN = re.compile(r"[a-z0-9][a-z0-9_-]{0,47}")
CARD = re.compile(r"^[ \t]*<PricingCard\b([^<>]*?)>(.*?)^[ \t]*</PricingCard>", re.M | re.S)
ATTRIBUTE = re.compile(r'^[ \t]*(name|price|interval|priceEyebrow|footnoteLabel)="([^"\r\n]*)"[ \t]*$', re.M)
DISPLAY_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9 ._+-]{0,15}")
USD_AMOUNT = r"\$([0-9]{1,4}(?:\.[0-9]{1,2})?)"


# rows: Sixteen 16-bit pixel rows in display order.
def encode_glyph(rows: tuple[int, ...]) -> str:
    return "".join(f"{row:04x}" for row in rows)


PLAN_ICONS = {key: encode_glyph(rows) for key, rows in PLAN_GLYPHS.items()}


# slug: Validated future plan identity with an exact match to a public pricing card.
def future_plan_icon(slug: str) -> str:
    bits = int.from_bytes(blake2s(slug.encode("ascii"), digest_size=2).digest(), "big")
    bits |= 1 << 8  # Keep the center of the glyph lit regardless of the slug.
    rows = [0, 0, 0]
    for y in range(5):
        row = 0
        for x in range(5):
            if bits & (1 << (y * 3 + min(x, 4 - x))):
                row |= 3 << (11 - x * 2)
        rows.extend((row, row))
    rows.extend((0, 0, 0))
    return encode_glyph(tuple(rows))


# attrs: Pricing attributes of one official English-language subscription card.
# body: Text below the price, which can explicitly enumerate Pro tiers.
def monthly_price(attrs: dict[str, str], body: str) -> str:
    interval = attrs.get("interval", "").replace(" ", "")
    amount = re.fullmatch(USD_AMOUNT, attrs.get("price", ""))
    if amount is None:
        return "-"
    if interval == "/user/month*":
        # The starred Business headline is annual billing. Prefer the explicit monthly amount.
        monthly = re.search(USD_AMOUNT + r" per user per month when billed monthly\b",
                            attrs.get("footnoteLabel", ""))
        return f"{monthly[1]} USD/user/mo" if monthly is not None else "-"
    if interval == "/user/month" and not attrs.get("priceEyebrow"):
        return f"{amount[1]} USD/user/mo"
    if interval != "/month":
        return "-"

    qualifier = attrs.get("priceEyebrow", "")
    if not qualifier:
        return f"{amount[1]} USD/mo"
    if qualifier != "From":
        return "-"
    tiers = re.search(r"\bPlans at ([^\r\n]+?) USD per month\b", body)
    if tiers is not None:
        prices = [re.fullmatch(USD_AMOUNT, value) for value in re.split(r",? or |, ", tiers[1])]
        if len(prices) >= 2 and all(prices) and prices[0][1] == amount[1]:
            label = "/".join(price[1] for price in prices) + " USD/mo"
            if len(label) <= 24:
                return label
    return f"From {amount[1]} USD/mo"


# document: Bounded official English Markdown source with structured PricingCard blocks.
# Raises ValueError for a missing, ambiguous or unsupported subscription catalog.
def parse_subscription_catalog(document: str) -> dict[str, tuple[str, str]]:
    catalog = {}
    for card in CARD.finditer(document):
        fields = ATTRIBUTE.findall(card[1])
        attrs = dict(fields)
        if len(attrs) != len(fields):
            raise ValueError("A subscription pricing card repeats a pricing attribute")
        name = attrs.get("name", "").strip()
        if DISPLAY_NAME.fullmatch(name) is None:
            continue
        slug = re.sub(r"[ -]+", "_", name.lower())
        if slug == "api_key":
            continue
        if slug in catalog:
            raise ValueError(f"Subscription pricing card has an ambiguous identity: {slug}")
        catalog[slug] = (f"ChatGPT {name}", monthly_price(attrs, card[2]))
    if not any(slug in SEED_PRICES and price != "-" for slug, (_, price) in catalog.items()):
        raise ValueError("Subscription pricing document has no supported public monthly prices")
    return catalog


# No parameters: downloads only the public pricing document without account credentials.
# Raises the original HTTP, transport, or decoding error, or ValueError for unsupported content.
def download_subscription_catalog() -> dict[str, tuple[str, str]]:
    request = Request(SOURCE, headers={"Accept": "text/markdown", "User-Agent": "Cody-Subscription-Catalog"})
    with urlopen(request, timeout=5) as response:
        if response.geturl() != SOURCE or response.headers.get_content_type() != "text/markdown":
            raise ValueError("Subscription pricing source returned an unexpected URL or content type")
        raw = response.read(MAX_DOCUMENT_BYTES + 1)
    if len(raw) > MAX_DOCUMENT_BYTES:
        raise ValueError("Subscription pricing source exceeds its byte bound")
    return parse_subscription_catalog(raw.decode("utf-8"))


class SubscriptionCatalog:
    # self: Optional public presentation metadata owned by the existing CCP broker.
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._worker: threading.Thread | None = None
        self._next_attempt = 0.0
        self._observed_at = 0.0
        self._catalog: dict[str, tuple[str, str]] | None = None

    # self: Starts at most one bounded public refresh per day, off the account reply path.
    # Raises RuntimeError when the optional worker cannot start.
    def start_refresh(self) -> None:
        with self._lock:
            now = time.monotonic()
            if now < self._next_attempt or self._worker is not None and self._worker.is_alive():
                return
            self._next_attempt = now + REFRESH_SECONDS
            self._worker = threading.Thread(target=self._refresh, name="ccp-subscriptions", daemon=True)
            self._worker.start()

    # self: Publishes a complete catalog atomically. Refresh errors preserve its original age.
    def _refresh(self) -> None:
        try:
            catalog = download_subscription_catalog()
        except (OSError, ValueError) as error:
            print(f"CCP subscriptions: public pricing refresh failed: {error}", file=sys.stderr, flush=True)
            return
        with self._lock:
            self._catalog = catalog
            self._observed_at = time.monotonic()

    # self: Read-only projection for an authenticated account sample, with no I/O.
    # plan: Exact App Server planType before compatibility aliases. None means no observed plan.
    def projection(self, plan: str | None) -> tuple[str, str, str]:
        if plan is None or plan == "unknown" or PLAN_TOKEN.fullmatch(plan) is None:
            return "-", "-", "-"
        slug = PLAN_ALIASES.get(plan, plan).replace("-", "_")
        with self._lock:
            live = self._catalog is not None and time.monotonic() - self._observed_at <= FRESH_SECONDS
            entry = self._catalog.get(slug) if live else None
            if entry is None:
                if slug not in PLAN_LABELS:
                    return "-", "-", "-"
                label = PLAN_LABELS[slug]
                seed_is_current = self._catalog is None and time.time() < SEED_VALID_UNTIL
                price = SEED_PRICES.get(slug, "-") if seed_is_current else "-"
            else:
                label, price = entry
        if plan in CONTRACT_PLANS:
            price = "-"
        icon = PLAN_ICONS.get(slug) or future_plan_icon(slug)
        return label, icon, price

    # self: Retires the optional worker when the existing broker shuts down.
    # Raises RuntimeError when an active network read exceeds the shutdown bound.
    def close(self) -> None:
        if self._worker is not None and self._worker.ident is not None:
            self._worker.join(timeout=7)
            if self._worker.is_alive():
                raise RuntimeError("Subscription pricing refresh did not stop within its shutdown bound")
