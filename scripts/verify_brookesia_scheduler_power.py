"""Static, hardware-free checks for the event-driven Brookesia scheduler policy."""

from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SCHEDULER_SOURCE = (
    ROOT
    / "third_party"
    / "esp-brookesia"
    / "utils"
    / "brookesia_lib_utils"
    / "src"
    / "task_scheduler.cpp"
)
SCHEDULER_HEADER = (
    ROOT
    / "third_party"
    / "esp-brookesia"
    / "utils"
    / "brookesia_lib_utils"
    / "include"
    / "brookesia"
    / "lib_utils"
    / "task_scheduler.hpp"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_text(text: str, signature: str) -> str:
    signature_at = text.find(signature)
    require(signature_at >= 0, f"missing function: {signature}")
    body_at = text.find("{", signature_at + len(signature))
    require(body_at >= 0, f"missing body: {signature}")

    depth = 0
    for index in range(body_at, len(text)):
        token = text[index]
        if token == "{":
            depth += 1
        elif token == "}":
            depth -= 1
            if depth == 0:
                return text[signature_at : index + 1]
    raise AssertionError(f"unterminated body: {signature}")


def main() -> int:
    source = SCHEDULER_SOURCE.read_text(encoding="utf-8")
    header = SCHEDULER_HEADER.read_text(encoding="utf-8")

    start = function_text(source, "bool TaskScheduler::start(")
    require("io_context_->poll()" in start, "native scheduler must retain non-blocking Asio polling")
    require("wait_for_work(observed_generation)" in start, "idle workers must enter the event wait")
    require("sleep_for(boost::chrono::milliseconds(poll_interval_ms))" not in start,
            "native scheduler still performs unconditional interval polling")
    require("io_context_->run()" not in start,
            "blocking Asio run is unsafe with esp-boost's non-selectable virtual pipe interrupter")

    wait = function_text(source, "void TaskScheduler::wait_for_work(")
    for fragment in (
        "get_next_timer_expiry()",
        "worker_wait_cv_.wait(lock)",
        "worker_wait_cv_.wait_for(lock, boost::chrono::microseconds(wait_us))",
        "external_executor_exposed_",
        "worker_poll_interval_ms_",
    ):
        require(fragment in wait, f"event wait is missing {fragment}")
    require("wait_for(lock, boost::chrono::microseconds(wait_us), should_wake)" not in wait,
            "Boost's predicate timed wait wakes every 100 ms on the ESP realtime-clock fallback")

    notify = function_text(source, "void TaskScheduler::notify_workers(")
    require("worker_wake_generation_.fetch_add" in notify, "wake generation is not advanced")
    require("worker_wait_cv_.notify_all()" in notify, "blocked workers are not notified")

    post = function_text(source, "bool TaskScheduler::post_internal(")
    require(post.rfind("notify_workers()") > post.rfind("boost::asio::post"),
            "posted work must notify workers after entering the Asio queue")

    for signature in (
        "void TaskScheduler::schedule_once(",
        "void TaskScheduler::schedule_periodic(",
    ):
        body = function_text(source, signature)
        for fragment in (
            "worker_deadline_generation",
            "worker_deadline = deadline",
            "clear_timer_deadline(handle, deadline_generation)",
            "notify_workers()",
        ):
            require(fragment in body, f"{signature} is missing {fragment}")

    clear_deadline = function_text(source, "void TaskScheduler::clear_timer_deadline(")
    require("worker_deadline_generation == expected_generation" in clear_deadline,
            "an expired callback could erase a newer timer deadline")
    require("worker_deadline.reset()" in clear_deadline,
            "expired timers must be removed before long callbacks execute")

    for signature in (
        "void TaskScheduler::stop(",
        "bool TaskScheduler::restart_timer(",
        "void TaskScheduler::cancel_internal(",
        "bool TaskScheduler::suspend_internal(",
        "bool TaskScheduler::resume_internal(",
    ):
        require("notify_workers()" in function_text(source, signature),
                f"timer/work state change does not wake workers: {signature}")

    require("external_executor_exposed_.store(true" in header,
            "raw executor exposure must enable compatibility polling")
    require("notify_workers();" in function_text(header, "std::shared_ptr<Executor> get_executor()"),
            "raw executor exposure must wake workers to change wait mode")

    # No in-tree caller currently escapes the raw executor. If one is added, this check forces an
    # explicit power review because that scheduler intentionally falls back to configured polling.
    raw_executor_consumers: list[Path] = []
    for base in (ROOT / "components", ROOT / "main", ROOT / "third_party" / "esp-brookesia"):
        for path in base.rglob("*"):
            if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"} or path in {
                SCHEDULER_HEADER,
                SCHEDULER_SOURCE,
            }:
                continue
            if re.search(r"(?:\.|->)get_executor\(\)", path.read_text(encoding="utf-8", errors="ignore")):
                raw_executor_consumers.append(path)
    require(not raw_executor_consumers,
            "raw TaskScheduler executor consumers require a power review: "
            + ", ".join(str(path.relative_to(ROOT)) for path in raw_executor_consumers))

    print("Brookesia scheduler power policy verification: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
