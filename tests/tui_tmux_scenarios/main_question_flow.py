"""The tmux TUI smoke scenario for main question flow."""

from __future__ import annotations

import re

from tui_smoke_helpers import (
    SmokeContext,
    save_evidence,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_absent,
)
from .common import _finish_main


def scenario_main_question_flow(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    root = ctx.root
    workspace = ctx.workspace
    fake_models = (
        '{"default_provider":"moonshot","default_model":"ava-tui-fake",'
        '"models":[{"provider":"moonshot","id":"ava-tui-fake","name":"AVA TUI Fake","family":"fake",'
        '"context_window_tokens":8192,"max_output_tokens":1024,"supports_tools":true,'
        '"supports_streaming":false,"supports_reasoning":false,"reports_usage":true}]}\n'
    )
    ctx.ava_config.joinpath("models.json").write_text(fake_models, encoding="utf-8")

    def launch_question_session(label: str, scenario: str, width: int = 120, height: int = 32) -> str:
        provider = ctx.start_fake_provider(label, delay_ms=0, scenario=scenario)
        command = ctx.fake_provider_command(
            provider,
            home=ctx.home,
            config=ctx.config,
            state=ctx.state,
            data=ctx.data,
        )
        session = ctx.session_name(label)
        ctx.launch_ava(session, workspace=workspace, command=command, width=width, height=height)
        wait_for(tmux_exe, session, r"Type a message", f"{label} initial TUI frame")
        return session

    def assert_question_frame(screen: str, session: str, width: int, height: int, label: str) -> None:
        dimensions = tmux(
            tmux_exe, "display-message", "-p", "-t", session, "#{window_width},#{window_height}"
        ).stdout.strip()
        lines = screen.splitlines()
        if dimensions != f"{width},{height}" or len(lines) != height or any(len(line) > width for line in lines):
            raise RuntimeError(f"{label} did not retain an exact {width}x{height} frame\nscreen:\n{screen}")
        if "\x1b" in screen or any(ord(character) < 32 and character != "\n" for character in screen):
            raise RuntimeError(f"{label} contained ESC or unexpected C0 controls\nscreen:\n{screen}")
        if "[" in screen or "]" in screen or "---" in screen or re.search(r"(?m)^\s*--\s", screen):
            raise RuntimeError(f"{label} retained obsolete bracket or dashed question chrome\nscreen:\n{screen}")
        if "call_question" in screen or "request_id" in screen:
            raise RuntimeError(f"{label} exposed raw question metadata\nscreen:\n{screen}")

    single_session = launch_question_session("question-single", "question-tool")
    send_literal(tmux_exe, single_session, "ask one question")
    send_keys(tmux_exe, single_session, "Enter")
    single = wait_for(
        tmux_exe,
        single_session,
        r"(?s)\? Pick.*Continue\?.*› 1\. Yes.*Custom: type to answer",
        "single question dock",
    )
    assert_question_frame(single, single_session, 120, 32, "single question dock")
    save_evidence(root, "frontend-f5-question-single", single)
    single_row = next(
        ((index + 1, line) for index, line in enumerate(single.splitlines()) if "1. Yes" in line),
        None,
    )
    if single_row is None:
        raise RuntimeError(f"single question did not expose a clickable option\nscreen:\n{single}")
    single_row_number, single_row_text = single_row
    single_column = single_row_text.index("1. Yes") + 1
    send_literal(tmux_exe, single_session, f"\x1b[<0;{single_column};{single_row_number}M")
    wait_for(
        tmux_exe,
        single_session,
        r"after question reply",
        "single question SGR mouse resolution",
    )
    single_resolved = wait_for_absent(tmux_exe, single_session, r"Esc stop", "single question settled idle state")
    if "? Pick" in single_resolved or single_resolved.count("after question reply") != 1:
        raise RuntimeError(f"single mouse resolution left the dock open or duplicated the reply\nscreen:\n{single_resolved}")
    _finish_main(tmux_exe, single_session)

    multi_session = launch_question_session("question-multi", "question-tool-multi")
    send_literal(tmux_exe, multi_session, "ask a multi question")
    send_keys(tmux_exe, multi_session, "Enter")
    multi = wait_for(
        tmux_exe,
        multi_session,
        r"(?s)\? Pick.*Choose providers.*› 1\. · Alpha.*2\. · Beta",
        "multi question dock",
    )
    send_keys(tmux_exe, multi_session, "Space", "Down")
    multi_keyboard = wait_for(
        tmux_exe,
        multi_session,
        r"(?s)1\. ✓ Alpha.*› 2\. · Beta",
        "multi question keyboard toggle and selection",
    )
    beta_row = next(
        ((index + 1, line) for index, line in enumerate(multi_keyboard.splitlines()) if "2. · Beta" in line),
        None,
    )
    if beta_row is None:
        raise RuntimeError(f"multi question did not expose a clickable Beta option\nscreen:\n{multi_keyboard}")
    beta_row_number, beta_row_text = beta_row
    beta_column = beta_row_text.index("2. · Beta") + 1
    send_literal(tmux_exe, multi_session, f"\x1b[<0;{beta_column};{beta_row_number}M")
    multi_mouse = wait_for(
        tmux_exe,
        multi_session,
        r"(?s)1\. ✓ Alpha.*› 2\. ✓ Beta",
        "multi question SGR mouse toggle",
    )

    tmux(tmux_exe, "resize-window", "-t", multi_session, "-x", "80", "-y", "24")
    multi_narrow = wait_for(
        tmux_exe,
        multi_session,
        r"(?s)\? Pick.*1\. ✓ Alpha.*› 2\. ✓ Beta.*AVA TUI Fake",
        "multi question narrow reflow",
    )
    assert_question_frame(multi_narrow, multi_session, 80, 24, "multi question narrow reflow")
    save_evidence(root, "frontend-f5-question-multi-narrow", multi_narrow)

    tmux(tmux_exe, "resize-window", "-t", multi_session, "-x", "100", "-y", "12")
    multi_short = wait_for(
        tmux_exe,
        multi_session,
        r"(?s)\? Pick.*1\. ✓ Alpha.*› 2\. ✓ Beta.*AVA TUI Fake",
        "multi question short reflow",
    )
    assert_question_frame(multi_short, multi_session, 100, 12, "multi question short reflow")
    save_evidence(root, "frontend-f5-question-short", multi_short)
    send_keys(tmux_exe, multi_session, "Enter")
    wait_for(
        tmux_exe,
        multi_session,
        r"after multi question reply",
        "multi question resolution",
    )
    multi_resolved = wait_for_absent(tmux_exe, multi_session, r"Esc stop", "multi question settled idle state")
    if "? Pick" in multi_resolved or multi_resolved.count("after multi question reply") != 1:
        raise RuntimeError(f"multi resolution left the dock open or duplicated the reply\nscreen:\n{multi_resolved}")
    _finish_main(tmux_exe, multi_session)
