#!/usr/bin/env python3
"""Deterministic regression tests for the startup-overview close wait.

The full 27-terminal gate caught ``ava_tui.tmux_smoke_startup_overview``
failing at 0.35 s with an entirely blank captured screen: the scenario closed
the overview with Escape and waited only for ``Startup overview`` to disappear.
A transient blank repaint between overview teardown and the composer redraw
satisfies that absence, so the immediately following composer-chrome assertion
failed on the same blank frame. This was a test-design defect, not an app
failure: closing the overview must wait for one frame that both dropped the
overview and restored the composer placeholder.

These tests exercise the real scenario helper (``_wait_for_closed_overview``)
against scripted pane captures with a fake monotonic clock, so the persistent
timeout cases prove bounded polling without any real eight-second sleeps.
"""

from __future__ import annotations

import pathlib
import re
import sys
import types
import unittest
from contextlib import ExitStack
from unittest import mock

# The scenario package imports its sibling harness modules (tui_smoke_helpers,
# fake_provider, test_timing_trace) by top-level name, so the tests/ directory
# that owns the package must precede the script directory on sys.path.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

import tui_smoke_helpers
from tui_smoke_helpers import POLL_INTERVAL, wait_for_absent
from tui_tmux_scenarios import startup_overview

# Modeled after captured pane text: the composer chrome line and the expanded
# overview title row, never the full terminal frames the smoke scenarios see.
BLANK_FRAME = ""
COMPOSER_FRAME = "│  Type a message\n"
OVERVIEW_FRAME = "Startup overview\nMode  Default\nProvider  moonshot\n"
MIXED_FRAME = "Startup overview\nType a message\n"

CLOSED_LABEL = "overview closed via Esc"
CLIENT = object()
SESSION = "unit-test-session"
LIVE_SESSION = types.SimpleNamespace(returncode=0)


class FakePane:
    """Scripted pane captures: replay queued frames, then repeat the last one."""

    def __init__(self, frames: list[str]) -> None:
        if not frames:
            raise ValueError("FakePane needs at least one frame")
        self._frames = list(frames)
        self.calls = 0

    def __call__(self, tmux_client: object, session: str) -> str:
        self.calls += 1
        if len(self._frames) > 1:
            return self._frames.pop(0)
        return self._frames[0]


class FakeTime:
    """Monotonic clock advanced only by ``sleep``, making timeouts instant."""

    def __init__(self) -> None:
        self.now = 0.0

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


def _harness(frames: list[str]) -> tuple[FakePane, FakeTime, ExitStack]:
    """Patch the helper-module wait primitives the scenario wait really calls.

    ``tui_smoke_helpers.wait_for_screen_state`` resolves ``capture``, ``tmux``,
    and ``time`` through its own module globals, so patching them there drives
    the real wait machinery deterministically.
    """

    pane = FakePane(frames)
    clock = FakeTime()
    stack = ExitStack()
    stack.enter_context(mock.patch.object(tui_smoke_helpers, "capture", side_effect=pane))
    stack.enter_context(mock.patch.object(tui_smoke_helpers, "tmux", return_value=LIVE_SESSION))
    stack.enter_context(mock.patch.object(tui_smoke_helpers, "time", clock))
    return pane, clock, stack


class StartupOverviewCloseWaitTests(unittest.TestCase):
    """Exercise the real ``_wait_for_closed_overview`` against scripted frames."""

    def test_blank_transient_does_not_complete_the_wait(self) -> None:
        pane, clock, stack = _harness([BLANK_FRAME, BLANK_FRAME, COMPOSER_FRAME])
        with stack:
            frame = startup_overview._wait_for_closed_overview(CLIENT, SESSION, CLOSED_LABEL)
        # Completion requires the composer in the same captured frame as the
        # overview absence, so both blank repaints are polled past.
        self.assertEqual(frame, COMPOSER_FRAME)
        self.assertEqual(pane.calls, 3)
        self.assertLess(clock.now, 1.0)

    def test_composer_frame_with_overview_still_visible_does_not_complete(self) -> None:
        pane, clock, stack = _harness([MIXED_FRAME, MIXED_FRAME, COMPOSER_FRAME])
        with stack:
            frame = startup_overview._wait_for_closed_overview(CLIENT, SESSION, CLOSED_LABEL)
        # A frame that still contains the overview title is not closed yet,
        # even when the composer placeholder has already been repainted.
        self.assertEqual(frame, COMPOSER_FRAME)
        self.assertEqual(pane.calls, 3)
        self.assertLess(clock.now, 1.0)

    def test_persistent_blank_missing_composer_times_out_on_fake_clock(self) -> None:
        pane, clock, stack = _harness([BLANK_FRAME])
        with stack:
            with self.assertRaises(RuntimeError) as raised:
                startup_overview._wait_for_closed_overview(CLIENT, SESSION, CLOSED_LABEL)
        message = str(raised.exception)
        self.assertIn(f"timed out waiting for {CLOSED_LABEL}", message)
        self.assertIn(BLANK_FRAME, message)
        # The default eight-second wait was fully consumed by the fake clock;
        # the real wall-clock cost stays tiny because sleep is faked.
        self.assertGreaterEqual(clock.now, 8.0)
        self.assertLess(clock.now, 8.0 + 3 * POLL_INTERVAL)
        self.assertGreaterEqual(pane.calls, 399)

    def test_persistent_still_overview_times_out_on_fake_clock(self) -> None:
        pane, clock, stack = _harness([OVERVIEW_FRAME])
        with stack:
            with self.assertRaises(RuntimeError) as raised:
                startup_overview._wait_for_closed_overview(CLIENT, SESSION, CLOSED_LABEL)
        message = str(raised.exception)
        self.assertIn(f"timed out waiting for {CLOSED_LABEL}", message)
        self.assertIn("Startup overview", message)
        self.assertGreaterEqual(clock.now, 8.0)
        self.assertLess(clock.now, 8.0 + 3 * POLL_INTERVAL)
        self.assertGreaterEqual(pane.calls, 399)

    def test_absence_only_wait_accepted_the_blank_repaint_that_new_wait_rejects(self) -> None:
        # Regression documentation: the previous close wait (absence only)
        # completes on the transient blank repaint, which is exactly the frame
        # that made the 27-terminal gate fail the composer-chrome assertion.
        pane, _, stack = _harness([BLANK_FRAME])
        with stack:
            frame = wait_for_absent(CLIENT, SESSION, r"Startup overview", "old absence-only close wait")
        self.assertEqual(frame, BLANK_FRAME)
        self.assertEqual(pane.calls, 1)
        self.assertNotIn("Type a message", frame)

        # The same persistent blank stream never satisfies the new wait, which
        # keeps polling until a composer frame appears or the bound expires.
        _, clock, stack = _harness([BLANK_FRAME])
        with stack:
            with self.assertRaises(RuntimeError):
                startup_overview._wait_for_closed_overview(CLIENT, SESSION, CLOSED_LABEL)
        self.assertGreaterEqual(clock.now, 8.0)

    def test_scenario_uses_the_wait_at_both_escape_close_sites(self) -> None:
        source = pathlib.Path(startup_overview.__file__).read_text(encoding="utf-8")
        bindings = re.findall(
            r'^    (closed|restored) = _wait_for_closed_overview\(tmux_exe, session, "[^"]+"\)\s*$',
            source,
            re.MULTILINE,
        )
        self.assertEqual(sorted(bindings), ["closed", "restored"])
        self.assertEqual(len(bindings), 2)


if __name__ == "__main__":
    unittest.main()
