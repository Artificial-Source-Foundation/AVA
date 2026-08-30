"""Opt-in, stream-safe timing traces for Python test harnesses.

The trace is enabled only when ``AVA_TEST_TIMING_DIR`` names an absolute
directory. Records are written directly to a private, invocation-unique JSONL
file so concurrent tests and repeated runs cannot overwrite one another.
Labels must describe test phases and must never contain payloads or paths.
"""

from __future__ import annotations

import contextvars
import functools
import json
import os
import pathlib
import re
import time
import uuid
from contextlib import AbstractContextManager
from typing import Callable, ParamSpec, TypeVar


_P = ParamSpec("_P")
_R = TypeVar("_R")
_active_trace: "TimingTrace | None" = None
_active_spans: contextvars.ContextVar[tuple["TimingSpan", ...]] = contextvars.ContextVar(
    "ava_test_timing_spans", default=()
)


def _safe_component(value: str) -> str:
    """Return a bounded filename component derived from a test identity."""

    safe = re.sub(r"[^A-Za-z0-9_.-]+", "-", value).strip(".-")
    return (safe or "unnamed-test")[:120]


class TimingTrace:
    """Write flushed JSONL timing records for one test invocation.

    The output file is opened with mode 0600 and an exclusive create. Records
    contain only caller-authored phase labels, safe identity, outcomes, and
    numeric timing metadata; exception messages and test data are never stored.
    """

    def __init__(self, output_directory: pathlib.Path, test_name: str) -> None:
        if not output_directory.is_absolute():
            raise RuntimeError("AVA_TEST_TIMING_DIR must be an absolute path")
        output_directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        if output_directory.is_symlink() or not output_directory.is_dir():
            raise RuntimeError("AVA_TEST_TIMING_DIR must name a non-symlink directory")

        self.test_name = _safe_component(test_name)
        self.run_id = uuid.uuid4().hex
        self.started = time.monotonic()
        self.sequence = 0
        filename = f"{self.test_name}.{os.getpid()}.{self.run_id}.timing.jsonl"
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
        if hasattr(os, "O_CLOEXEC"):
            flags |= os.O_CLOEXEC
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        descriptor = os.open(output_directory / filename, flags, 0o600)
        self.path = output_directory / filename
        self._file = os.fdopen(descriptor, "w", encoding="utf-8")
        self.write("trace", "test invocation", event="begin")

    def write(self, kind: str, label: str, *, event: str, **fields: object) -> None:
        """Append and flush one metadata-only record for a timing event."""

        self.sequence += 1
        record: dict[str, object] = {
            "schema": 1,
            "sequence": self.sequence,
            "run_id": self.run_id,
            "test": self.test_name,
            "event": event,
            "kind": kind,
            "label": label,
            "elapsed_ms": round((time.monotonic() - self.started) * 1000, 3),
        }
        record.update(fields)
        self._file.write(json.dumps(record, separators=(",", ":"), sort_keys=True) + "\n")
        self._file.flush()

    def span(self, kind: str, label: str) -> "TimingSpan":
        """Create a span for one caller-authored, non-sensitive phase label."""

        return TimingSpan(self, kind, label)

    def close(self, outcome: str = "success") -> None:
        """Finish and close the invocation trace; repeated closes are harmless."""

        if self._file.closed:
            return
        self.write(
            "trace",
            "test invocation",
            event="end",
            outcome=outcome,
            duration_ms=round((time.monotonic() - self.started) * 1000, 3),
        )
        self._file.close()


class TimingSpan(AbstractContextManager["TimingSpan"]):
    """Record begin/end events, outcome, duration, and polling work for a phase."""

    def __init__(self, trace: TimingTrace, kind: str, label: str) -> None:
        self.trace = trace
        self.kind = kind
        self.label = label
        self.started = 0.0
        self.polls = 0
        self._token: contextvars.Token[tuple[TimingSpan, ...]] | None = None

    def __enter__(self) -> "TimingSpan":
        self.started = time.monotonic()
        self.trace.write(self.kind, self.label, event="begin")
        self._token = _active_spans.set((*_active_spans.get(), self))
        return self

    def poll(self) -> None:
        """Count one bounded polling iteration without emitting another record."""

        self.polls += 1

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> bool:
        if self._token is not None:
            _active_spans.reset(self._token)
        outcome = "success"
        if exc_type is not None:
            outcome = "failure" if isinstance(exc, Exception) else "cancelled"
        self.trace.write(
            self.kind,
            self.label,
            event="end",
            outcome=outcome,
            duration_ms=round((time.monotonic() - self.started) * 1000, 3),
            polls=self.polls,
        )
        return False


def configure_test_timing(fallback_test_name: str) -> TimingTrace | None:
    """Configure the process-local trace from AVA_TEST_TIMING_DIR.

    ``fallback_test_name`` is used only when CTest did not provide
    ``AVA_TEST_NAME``. An absent or empty timing directory disables tracing.
    """

    global _active_trace
    configured_directory = os.environ.get("AVA_TEST_TIMING_DIR")
    if not configured_directory:
        _active_trace = None
        return None
    test_name = os.environ.get("AVA_TEST_NAME") or fallback_test_name
    _active_trace = TimingTrace(pathlib.Path(configured_directory), test_name)
    return _active_trace


def timing_span(kind: str, label: str) -> AbstractContextManager[TimingSpan | None]:
    """Return an active timing span or a no-op context when tracing is disabled."""

    if _active_trace is None:
        return _NullTimingSpan()
    return _active_trace.span(kind, label)


class _NullTimingSpan(AbstractContextManager[None]):
    """Provide the timing-span interface without work when tracing is disabled."""

    def __enter__(self) -> None:
        return None

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> bool:
        return False


def timing_poll() -> None:
    """Count a poll on the innermost active span, if timing is enabled."""

    spans = _active_spans.get()
    if spans:
        spans[-1].poll()


def timed_operation(kind: str, *, label_argument: str | None = "label", default_label: str = "operation") -> Callable:
    """Decorate a harness operation using a safe descriptive argument as label."""

    def decorate(function: Callable[_P, _R]) -> Callable[_P, _R]:
        @functools.wraps(function)
        def wrapped(*args: _P.args, **kwargs: _P.kwargs) -> _R:
            label = default_label
            if label_argument is not None:
                argument_names = function.__code__.co_varnames[: function.__code__.co_argcount]
                if label_argument in kwargs:
                    label = str(kwargs[label_argument])
                elif label_argument in argument_names:
                    index = argument_names.index(label_argument)
                    if index < len(args):
                        label = str(args[index])
            with timing_span(kind, label):
                return function(*args, **kwargs)

        return wrapped

    return decorate
