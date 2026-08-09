#!/usr/bin/env python3
"""Dispatch the independent opt-in tmux TUI smoke scenarios."""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import signal
import sys

from tui_smoke_helpers import SKIP, SmokeContext, enabled
from tui_tmux_scenarios import SCENARIOS, SCENARIO_HANDLERS


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", required=True, choices=SCENARIOS)
    parser.add_argument("--ava", required=True)
    parser.add_argument("--fake-provider", required=True)
    parser.add_argument("--fake-mermaid-helper", required=True)
    parser.add_argument("--root", required=True)
    args = parser.parse_args()

    if not enabled(os.environ.get("AVA_TUI_TMUX_SMOKE")):
        print("skipping tmux TUI smoke; set AVA_TUI_TMUX_SMOKE=1 to run")
        return SKIP

    tmux_exe = shutil.which("tmux")
    if tmux_exe is None:
        print("skipping tmux TUI smoke; tmux is not installed")
        return SKIP

    ava_exe = pathlib.Path(args.ava).absolute()
    fake_provider_exe = pathlib.Path(args.fake_provider).absolute()
    fake_mermaid_helper_exe = pathlib.Path(args.fake_mermaid_helper).absolute()
    if not ava_exe.exists():
        raise RuntimeError(f"AVA executable does not exist: {ava_exe}")
    if not fake_provider_exe.exists():
        raise RuntimeError(f"fake provider executable does not exist: {fake_provider_exe}")
    if not fake_mermaid_helper_exe.exists():
        raise RuntimeError(f"fake Mermaid helper executable does not exist: {fake_mermaid_helper_exe}")

    context: SmokeContext | None = None

    def stop_scenario(signum: int, _frame: object) -> None:
        if context is not None:
            context.close()
        raise SystemExit(128 + signum)

    for handled_signal in (signal.SIGINT, signal.SIGTERM):
        signal.signal(handled_signal, stop_scenario)
    if hasattr(signal, "SIGALRM"):
        signal.signal(signal.SIGALRM, stop_scenario)
        # Leave CTest ten seconds to deliver SIGTERM and verify cleanup before
        # its outer timeout force-kills the process tree.
        signal.alarm(50)

    try:
        context = SmokeContext(
            scenario=args.scenario,
            root=pathlib.Path(args.root),
            ava_exe=ava_exe,
            fake_provider_exe=fake_provider_exe,
            fake_mermaid_helper_exe=fake_mermaid_helper_exe,
            tmux_exe=tmux_exe,
        )
        SCENARIO_HANDLERS[args.scenario](context)
    finally:
        if hasattr(signal, "SIGALRM"):
            signal.alarm(0)
        if context is not None:
            context.close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
