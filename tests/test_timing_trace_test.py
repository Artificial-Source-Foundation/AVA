#!/usr/bin/env python3
"""Regression tests for stream-safe Python test timing traces."""

from __future__ import annotations

import json
import os
import pathlib
import stat
import tempfile

from test_timing_trace import configure_test_timing, timing_poll, timing_span


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def records(path: pathlib.Path) -> list[dict[str, object]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        disabled = root / "disabled"
        os.environ.pop("AVA_TEST_TIMING_DIR", None)
        require(configure_test_timing("disabled") is None, "unset timing directory did not disable tracing")
        with timing_span("wait", "disabled wait"):
            timing_poll()
        require(not disabled.exists(), "disabled timing unexpectedly created output")

        output = root / "timings"
        os.environ["AVA_TEST_TIMING_DIR"] = str(output)
        os.environ["AVA_TEST_NAME"] = "../unsafe test/name"
        first = configure_test_timing("fallback")
        require(first is not None, "configured timing did not create a trace")
        with timing_span("wait", "provider became ready"):
            timing_poll()
            timing_poll()
        try:
            with timing_span("wait", "expected failure"):
                raise RuntimeError("SECRET-PAYLOAD-MUST-NOT-LEAK /private/path")
        except RuntimeError:
            pass
        first.close()

        require(first.path.parent == output, "unsafe test identity escaped the timing directory")
        require(stat.S_IMODE(first.path.stat().st_mode) == 0o600, "timing file mode is not 0600")
        first_records = records(first.path)
        require(
            all(record["run_id"] == first.run_id for record in first_records), "run identity changed within a trace"
        )
        require(
            all(float(record["elapsed_ms"]) >= 0 for record in first_records), "trace contains a negative elapsed time"
        )
        elapsed = [float(record["elapsed_ms"]) for record in first_records]
        require(elapsed == sorted(elapsed), "trace elapsed times are not monotonic")
        wait_ends = [record for record in first_records if record["kind"] == "wait" and record["event"] == "end"]
        require(
            wait_ends[0]["outcome"] == "success" and wait_ends[0]["polls"] == 2,
            "successful wait metadata is incomplete",
        )
        require(wait_ends[1]["outcome"] == "failure", "failed wait outcome was not recorded")
        serialized = first.path.read_text(encoding="utf-8")
        require(
            "SECRET-PAYLOAD" not in serialized and "/private/path" not in serialized,
            "exception payload leaked into timing output",
        )

        second = configure_test_timing("fallback")
        require(second is not None, "repeat trace was not created")
        second.close()
        require(
            first.path != second.path and first.run_id != second.run_id,
            "repeated invocation overwrote a timing trace",
        )
        require(len(list(output.glob("*.timing.jsonl"))) == 2, "repeat traces did not remain independently readable")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
