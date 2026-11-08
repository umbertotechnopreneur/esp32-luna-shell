"""Read-only Codex account metrics, observed on demand by the existing CCP owner."""

from __future__ import annotations

from dataclasses import dataclass, replace
from datetime import date, timedelta
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import threading
import time

from ccp_app_server import NativeAppServer, NativeInputRejected


WEEKLY_WINDOW_MINUTES = 7 * 24 * 60
MAX_RESET_UNIX_SECONDS = 253_402_300_799  # 9999-12-31T23:59:59Z
POLL_SECONDS = 60
FRESH_SECONDS = 90
MAX_UINT32 = (1 << 32) - 1
MAX_UINT64 = (1 << 64) - 1
EPOCH_DAY = date(1970, 1, 1)
DAILY_BUCKET_COUNT = 8
DAY_SECONDS = 24 * 60 * 60
QUARTER_SECONDS = 15 * 60
QUARTERS_PER_DAY = DAY_SECONDS // QUARTER_SECONDS
READ_FAILURES = (EOFError, OSError, RuntimeError, ValueError, subprocess.SubprocessError)
# These App Server plan types exceed the CCP1 plan field's 24-character bound.
PLAN_WIRE_ALIASES = {
    "self_serve_business_prolite": "business",
    "self_serve_business_usage_based": "business",
    "enterprise_cbp_automation": "enterprise",
    "enterprise_cbp_usage_based": "enterprise",
}


@dataclass(frozen=True)
class AllowanceWindow:
    reset_unix_seconds: int
    remaining_bps: int
    observed_at: float


@dataclass(frozen=True)
class DailyTokenSample:
    utc_slot: int
    daily_total: int | None


@dataclass(frozen=True)
class QuarterTokens:
    series: str
    quarter: int
    tokens: int | None


@dataclass(frozen=True)
class AccountDetails:
    observed_at: float
    short_window: AllowanceWindow | None = None
    short_minutes: int | None = None
    usage_anchor: int | None = None
    daily_tokens: tuple[int | None, ...] = ()
    streak: int | None = None
    best_streak: int | None = None
    credits: int | str | None = None
    vouchers: int | None = None
    plan: str | None = None
    messages: int | None = None
    quarter_tokens: QuarterTokens | None = None
    raw_plan: str | None = None


@dataclass(frozen=True)
class AccountObservation:
    weekly: AllowanceWindow | None = None
    details: AccountDetails | None = None


# No parameters: locates Codex on the inherited executable search path.
# Raises FileNotFoundError when no installed executable is available.
def find_native_codex() -> str:
    name = "codex.exe" if os.name == "nt" else "codex"
    paths = [Path(directory.strip('"')) for directory in os.get_exec_path() if directory]
    if os.name == "nt" and os.environ.get("LOCALAPPDATA"):
        paths.append(Path(os.environ["LOCALAPPDATA"]) / "Microsoft" / "WinGet" / "Links")
    for directory in paths:
        candidate = directory / name
        # Never search an implicit working directory or invoke a shell wrapper.
        if directory.is_absolute() and candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    raise FileNotFoundError("Installed Codex executable unavailable to the CCP owner")


# limits: Result of the documented account/rateLimits/read operation.
def codex_limit_bucket(limits: dict | None) -> dict | None:
    if not isinstance(limits, dict):
        return None
    buckets = limits.get("rateLimitsByLimitId")
    if buckets is None or (isinstance(buckets, dict) and not buckets):
        bucket = limits.get("rateLimits")
        return bucket if isinstance(bucket, dict) and bucket.get("limitId") == "codex" else None
    bucket = buckets.get("codex") if isinstance(buckets, dict) else None
    return bucket if isinstance(bucket, dict) and bucket.get("limitId") in (None, "codex") else None

# window: One returned primary or secondary window of a known duration.
# observed_at: Original monotonic time of receipt of the rate limits.
def parse_allowance_window(window: dict, observed_at: float) -> AllowanceWindow | None:
    used = window.get("usedPercent")
    reset = window.get("resetsAt")
    if type(used) not in (int, float) or not 0 <= used <= 100:
        return None
    if type(reset) is not int or not 1 <= reset <= MAX_RESET_UNIX_SECONDS:
        return None
    return AllowanceWindow(reset, 10_000 - round(used * 100), observed_at)


# bucket: Verified Codex bucket, which may lack one or both duration windows.
# observed_at: Original monotonic time of receipt of the rate limits.
def read_allowance_windows(bucket: dict | None, observed_at: float) -> tuple[
        AllowanceWindow | None, AllowanceWindow | None, int | None]:
    windows: dict[int, list[dict]] = {}
    if bucket is not None:
        for name in ("primary", "secondary"):
            window = bucket.get(name)
            duration = window.get("windowDurationMins") if isinstance(window, dict) else None
            if type(duration) is int and 0 < duration <= WEEKLY_WINDOW_MINUTES:
                windows.setdefault(duration, []).append(window)
    weekly_rows = windows.pop(WEEKLY_WINDOW_MINUTES, [])
    weekly = parse_allowance_window(weekly_rows[0], observed_at) if len(weekly_rows) == 1 else None
    if not windows:
        return weekly, None, None
    duration = 300 if 300 in windows else min(windows)
    rows = windows[duration]
    short = parse_allowance_window(rows[0], observed_at) if len(rows) == 1 else None
    return weekly, short, duration if short is not None else None


# value: Untrusted numeric field from the public account response.
# maximum: Wire-domain upper bound.
def unsigned_field(value, maximum: int) -> int | None:
    return value if type(value) is int and 0 <= value <= maximum else None


# value: App Server planType from the verified ChatGPT account.
def read_plan_token(value: object) -> str | None:
    if not isinstance(value, str):
        return None
    plan = PLAN_WIRE_ALIASES.get(value, value)
    return plan if re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,23}", plan) is not None else None


# bucket: Verified Codex metered bucket containing optional workspace credits.
def read_credits(bucket: dict | None) -> int | str | None:
    credits = bucket.get("credits") if bucket is not None else None
    if not isinstance(credits, dict):
        return None
    if credits.get("unlimited") is True:
        return "unlimited"
    balance = credits.get("balance")
    if balance is None and credits.get("hasCredits") is False:
        return 0
    if type(balance) not in (int, float, str):
        return None
    text = str(balance)
    if len(text) > 32:
        return None
    try:
        amount = Decimal(text)
        if amount.is_finite() and 0 <= amount <= Decimal(MAX_UINT64) / 1_000:
            return int((amount * 1_000).to_integral_value(rounding=ROUND_HALF_UP))
    except InvalidOperation:
        return None
    return None


# usage: Optional account/usage/read result with service-provided date labels.
def read_daily_totals(usage: dict | None) -> dict[date, int | None]:
    buckets = usage.get("dailyUsageBuckets") if isinstance(usage, dict) else None
    if not isinstance(buckets, list):
        return {}
    observed: dict[date, int | None] = {}
    for bucket in buckets:
        label = bucket.get("startDate") if isinstance(bucket, dict) else None
        if not isinstance(label, str) or len(label) != 10:
            continue
        try:
            day = date.fromisoformat(label)
        except ValueError:
            continue
        if day < EPOCH_DAY or day.isoformat() != label:
            continue
        tokens = unsigned_field(bucket.get("tokens"), MAX_UINT64)
        observed[day] = tokens if day not in observed or observed[day] == tokens else None
    return observed


# observed: Service-provided daily totals. Missing or conflicting days stay unavailable.
# received_at: UTC Unix seconds when the service response arrived.
def read_daily_usage(observed: dict[date, int | None], received_at: int) -> tuple[int | None, tuple[int | None, ...]]:
    if not 0 <= received_at <= MAX_RESET_UNIX_SECONDS:
        return None, ()
    today = EPOCH_DAY + timedelta(days=received_at // DAY_SECONDS)
    latest = max((day for day in observed if day <= today), default=None)
    if latest is None:
        return None, ()
    anchor = (latest - EPOCH_DAY).days
    if anchor < DAILY_BUCKET_COUNT - 1:
        return None, ()
    tokens = tuple(observed.get(latest - timedelta(days=offset))
                   for offset in range(DAILY_BUCKET_COUNT - 1, -1, -1))
    return (anchor, tokens) if any(value is not None for value in tokens) else (None, ())


# observed: Confirmed service date labels and token totals from one successful response.
# received_at: Actual UTC Unix seconds when the read completed.
def read_daily_token_sample(observed: dict[date, int | None], received_at: int) -> DailyTokenSample | None:
    if not QUARTER_SECONDS <= received_at <= MAX_RESET_UNIX_SECONDS:
        return None
    utc_slot = received_at // QUARTER_SECONDS
    day = EPOCH_DAY + timedelta(days=utc_slot // QUARTERS_PER_DAY)
    return DailyTokenSample(utc_slot, observed.get(day))


# result: Optional account/workspaceMessages/read response. Only its count is retained.
def read_workspace_count(result: dict | None) -> int | None:
    if not isinstance(result, dict) or result.get("featureEnabled") is not True:
        return None
    messages = result.get("messages")
    if not isinstance(messages, list):
        return None
    identities = set()
    for message in messages:
        identity = message.get("messageId") if isinstance(message, dict) else None
        if (not isinstance(identity, str) or not identity or identity in identities
                or message.get("archivedAt") is not None):
            return None
        identities.add(identity)
    return len(identities)


class AccountLimitClient(NativeAppServer):
    methods = frozenset({"initialize", "account/read", "account/rateLimits/read", "account/usage/read",
                         "account/workspaceMessages/read"})
    response_limit = 256 * 1024
    # The read-only child must never load a plugin that can renew CCP's native lease.
    child_options = ("-c", "features.plugins=false", "-c", "mcp_servers={}")

    # self: One read-only stdio child using the current native Codex sign-in.
    # Raises transport errors or ValueError when ChatGPT account data is unavailable.
    def read_account(self) -> dict:
        result = self.call("account/read", {"refreshToken": False})
        account = result.get("account") if isinstance(result, dict) else None
        if not isinstance(account, dict) or account.get("type") != "chatgpt":
            raise ValueError("A native Codex ChatGPT account is required for weekly allowance")
        return account

    # self: Account read owner; an unavailable optional method does not erase other data.
    # method: Fixed allowlisted read without parameters.
    # Raises original transport or unexpected protocol errors.
    def read_optional(self, method: str) -> dict | None:
        try:
            result = self.call(method, None)
        except NativeInputRejected as error:
            if not self.stop_event.is_set():
                print(f"CCP optional account read unavailable: {error}", file=sys.stderr, flush=True)
            return None
        return result if isinstance(result, dict) else None

    # self: Publishes account data only after verifying the same account surrounds the reads.
    # observe_account: Owner callback that retires the old in-memory series on an observed account change.
    # Raises original transport, authentication or schema errors.
    def read_observation(self, observe_account) -> tuple[AccountObservation, DailyTokenSample | None]:
        account = self.read_account()
        observe_account(account)
        account_at = time.monotonic()
        limits = self.read_optional("account/rateLimits/read")
        limits_at = time.monotonic()
        confirmed_account = self.read_account()
        observe_account(confirmed_account)
        if confirmed_account != account:
            raise ValueError("Native Codex account changed during the rate-limit read")

        bucket = codex_limit_bucket(limits)
        weekly, short, short_minutes = read_allowance_windows(bucket, limits_at)
        vouchers = limits.get("rateLimitResetCredits") if limits is not None else None
        voucher_count = unsigned_field(vouchers.get("availableCount"), MAX_UINT32) if isinstance(vouchers, dict) else None
        plan_type = account.get("planType")
        raw_plan = (plan_type if isinstance(plan_type, str) and
                    re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,47}", plan_type) is not None else None)
        plan = read_plan_token(raw_plan)
        credits = read_credits(bucket)
        details_at = None
        if raw_plan is not None:
            details_at = account_at
        elif short is not None or credits is not None or voucher_count is not None:
            details_at = limits_at
        details = AccountDetails(details_at if details_at is not None else limits_at,
                                 short_window=short, short_minutes=short_minutes,
                                 credits=credits, vouchers=voucher_count, plan=plan, raw_plan=raw_plan)
        baseline = AccountObservation(weekly, details if details_at is not None else None)

        try:
            usage = self.read_optional("account/usage/read")
            usage_received_at = time.time_ns() // 1_000_000_000
            usage_at = time.monotonic()
            workspace = self.read_optional("account/workspaceMessages/read")
            workspace_at = time.monotonic()
            confirmed_account = self.read_account()
        except READ_FAILURES as error:
            if not self.stop_event.is_set():
                print(f"CCP optional account details failed: {error}", file=sys.stderr, flush=True)
            return baseline, None
        observe_account(confirmed_account)
        if confirmed_account != account:
            raise ValueError("Native Codex account changed during the account details read")

        totals = read_daily_totals(usage)
        anchor, tokens = read_daily_usage(totals, usage_received_at)
        sample = read_daily_token_sample(totals, usage_received_at) if usage is not None else None
        summary = usage.get("summary") if usage is not None else None
        streak = unsigned_field(summary.get("currentStreakDays"), MAX_UINT32) if isinstance(summary, dict) else None
        best = unsigned_field(summary.get("longestStreakDays"), MAX_UINT32) if isinstance(summary, dict) else None
        messages = read_workspace_count(workspace)
        if details_at is None and (anchor is not None or streak is not None or best is not None or sample is not None):
            details_at = usage_at
        if details_at is None and messages is not None:
            details_at = workspace_at
        if details_at is None:
            return baseline, sample
        return AccountObservation(weekly, replace(details, observed_at=details_at, usage_anchor=anchor,
                                  daily_tokens=tokens, streak=streak, best_streak=best, messages=messages)), sample


class AccountUsageReader:
    # self: One demand-driven account reader owned by the existing CCP responder.
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._worker: threading.Thread | None = None
        self._cancel: threading.Event | None = None
        self._closed = False
        self._next_poll = 0.0
        self._observation = AccountObservation()
        self._account: dict | None = None
        self._series = secrets.randbelow(MAX_UINT64) + 1
        self._sample: DailyTokenSample | None = None
        self._quarter_tokens: QuarterTokens | None = None
        self._continuity_floor: int | None = None

    # self: Called only after admission of a physical device's authenticated request.
    # Raises RuntimeError if the optional refresh worker cannot start.
    def request(self) -> AccountObservation:
        with self._lock:
            if self._closed:
                return AccountObservation()
            now = time.monotonic()
            if now >= self._next_poll and (self._worker is None or not self._worker.is_alive()):
                self._next_poll = now + POLL_SECONDS
                self._cancel = threading.Event()
                self._worker = threading.Thread(target=self._refresh, args=(self._cancel,),
                                                name="CCP-account-usage", daemon=True)
                try:
                    self._worker.start()
                except RuntimeError:
                    self._observation = AccountObservation()
                    raise
            weekly = self._observation.weekly
            details = self._observation.details
            return AccountObservation(
                weekly if weekly is not None and 0 <= now - weekly.observed_at < FRESH_SECONDS else None,
                details if details is not None and 0 <= now - details.observed_at < FRESH_SECONDS else None)

    # self: Existing native-lease owner has lost evidence of an open Codex transport.
    def invalidate(self) -> None:
        with self._lock:
            self._observation = AccountObservation()
            if self._cancel is not None:
                self._cancel.set()

    # self: Existing CCP shutdown path, retiring only its own optional Codex read.
    # Raises RuntimeError if the worker fails to stop within the child-cleanup bound.
    def close(self) -> None:
        with self._lock:
            self._closed = True
            worker = self._worker
        self.invalidate()
        if worker is not None and worker.ident is not None:
            worker.join(timeout=9)
            if worker.is_alive():
                raise RuntimeError("CCP account usage worker did not stop")

    # self: Sole in-memory account and interval series owner.
    # account: Native account object used only for exact private comparison.
    # cancel: Current read's cancellation, which invalidates its authority to change the series.
    def _observe_account(self, account: dict, cancel: threading.Event) -> None:
        with self._lock:
            if self._closed or cancel.is_set():
                return
            if self._account is not None and account != self._account:
                series = secrets.randbelow(MAX_UINT64) + 1
                self._series = series if series != self._series else series % MAX_UINT64 + 1
                self._sample = None
                self._quarter_tokens = None
                self._continuity_floor = None
                self._observation = AccountObservation()
            self._account = account

    # self: Sole interval owner, called with its lock held after account verification.
    # sample: First available service observation for a UTC slot, including an observed gap.
    def _observe_quarter(self, sample: DailyTokenSample) -> QuarterTokens | None:
        previous = self._sample
        if previous is not None and sample.utc_slot < previous.utc_slot:
            # A backwards host clock cannot rewrite an earlier interval or move the current one.
            self._continuity_floor = None
            return None
        floor = self._continuity_floor
        if previous is not None and sample.utc_slot == previous.utc_slot:
            # Intermediate reads cannot replace the slot sample. A detected
            # reset still invalidates its continuity for the following slot.
            if floor is not None and sample.daily_total is not None:
                self._continuity_floor = sample.daily_total if sample.daily_total >= floor else None
            return self._quarter_tokens
        tokens = None
        if (previous is not None and sample.utc_slot == previous.utc_slot + 1
                and sample.utc_slot // QUARTERS_PER_DAY == previous.utc_slot // QUARTERS_PER_DAY
                and previous.daily_total is not None and sample.daily_total is not None
                and floor is not None and sample.daily_total >= floor):
            tokens = sample.daily_total - previous.daily_total
        self._sample = sample
        self._continuity_floor = sample.daily_total
        self._quarter_tokens = QuarterTokens(f"{self._series:016x}", sample.utc_slot - 1, tokens)
        return self._quarter_tokens

    # self: Single native read owner. Failures replace the observation with unavailable.
    # cancel: Cancellation for this one read, set when the native lease closes.
    def _refresh(self, cancel: threading.Event) -> None:
        observation = AccountObservation()
        sample = None
        if not cancel.is_set():
            try:
                client = AccountLimitClient(find_native_codex(), cancel,
                                            client_info={"name": "cody_account_usage", "version": "1"})
                try:
                    observation, sample = client.read_observation(
                        lambda account: self._observe_account(account, cancel))
                except BaseException as error:
                    try:
                        client.close()
                    except READ_FAILURES as cleanup_error:
                        error.add_note(f"Codex App Server cleanup failed: {cleanup_error}")
                    raise
                else:
                    client.close()
            except READ_FAILURES as error:
                observation = AccountObservation()
                sample = None
                if not cancel.is_set():
                    print(f"CCP account usage read failed: {error}", file=sys.stderr, flush=True)
        with self._lock:
            if not self._closed and not cancel.is_set():
                quarter_tokens = self._observe_quarter(sample) if sample is not None else self._quarter_tokens
                if observation.details is not None:
                    observation = replace(observation, details=replace(
                        observation.details, quarter_tokens=quarter_tokens))
                self._observation = observation
