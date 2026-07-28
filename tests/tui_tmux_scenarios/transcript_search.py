"""Real-terminal transcript search coverage in idle and active streaming states."""

from __future__ import annotations

import re

from tui_smoke_helpers import (
    SmokeContext,
    capture,
    save_evidence,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_screen_state,
    wait_for_session_exit,
)
from .common import _wait_for_normal_turn_request_count


def _numbered_lines(screen: str) -> list[int]:
    return [int(value) for value in re.findall(r"stream line (\d{3})", screen)]


def _modal_selected_row(screen: str) -> str:
    for line in screen.splitlines():
        stripped = line.strip()
        if stripped.startswith("›   ") and any(identity in stripped for identity in ("assistant", "user", "tool")):
            return stripped
    return ""


def _click_selected_row(ctx: SmokeContext, session: str, screen: str) -> None:
    row_text = _modal_selected_row(screen)
    if not row_text:
        raise RuntimeError(f"transcript search did not expose a selected row\nscreen:\n{screen}")
    for row, line in enumerate(screen.splitlines(), start=1):
        if row_text == line.strip():
            column = line.index("› ") + 2
            send_literal(ctx.tmux, session, f"\x1b[<0;{column};{row}M")
            return
    raise RuntimeError(f"could not locate selected transcript search row\nscreen:\n{screen}")


def scenario_transcript_search(ctx: SmokeContext) -> None:
    controls = ctx.root / "transcript-search-controls"
    controls.mkdir(mode=0o700)
    controls.chmod(0o700)
    models = (
        '{"default_provider":"moonshot","default_model":"ava-tui-fake",'
        '"models":[{"provider":"moonshot","id":"ava-tui-fake","name":"AVA TUI Fake","family":"fake",'
        '"context_window_tokens":8192,"max_output_tokens":1024,"supports_tools":false,'
        '"supports_streaming":true,"supports_reasoning":false,"reports_usage":true}]}\n'
    )
    ctx.active_ava_config.joinpath("models.json").write_text(models, encoding="utf-8")
    provider = ctx.start_fake_provider("transcript-search", delay_ms=20, scenario="streaming-scroll", target=controls)
    command = ctx.fake_provider_command(
        provider,
        home=ctx.active_home,
        config=ctx.active_config,
        state=ctx.active_state,
        data=ctx.active_data,
    )
    session = ctx.session_name("transcript-search")
    ctx.launch_ava(session, workspace=ctx.active_workspace, command=command, width=110, height=30)
    wait_for(ctx.tmux, session, r"Type a message|live session", "transcript-search initial frame")

    send_literal(ctx.tmux, session, "UNICODE-Ä transcript seed")
    send_keys(ctx.tmux, session, "Enter")
    _wait_for_normal_turn_request_count(provider.request_log, 1, "transcript-search provider request")
    paused = wait_for(ctx.tmux, session, r"stream line 029", "transcript-search paused active stream")
    paused_numbers = _numbered_lines(paused)

    send_literal(ctx.tmux, session, "/search")
    send_keys(ctx.tmux, session, "Enter")
    active_search = wait_for_screen_state(
        ctx.tmux,
        session,
        lambda screen: "Transcript search" in screen and "2 items" in screen and bool(_modal_selected_row(screen)),
        "active transcript search initial results",
    )
    selected_before = _modal_selected_row(active_search)
    if "assistant" not in selected_before:
        raise RuntimeError(f"transcript search did not initially select the newest chronological item\nscreen:\n{active_search}")
    send_keys(ctx.tmux, session, "Up")
    navigated = wait_for_screen_state(
        ctx.tmux,
        session,
        lambda screen: _modal_selected_row(screen) not in ("", selected_before),
        "active transcript search result navigation",
    )
    if "user" not in _modal_selected_row(navigated):
        raise RuntimeError(f"transcript search Up did not select the prior chronological item\nscreen:\n{navigated}")
    tmux(ctx.tmux, "resize-window", "-t", session, "-x", "96", "-y", "26")
    resized = wait_for(ctx.tmux, session, r"Transcript search.*2 items", "active transcript search resize refresh")
    if not _modal_selected_row(resized):
        raise RuntimeError(f"resize lost transcript search selection\nscreen:\n{resized}")
    tmux(ctx.tmux, "resize-window", "-t", session, "-x", "110", "-y", "30")
    wait_for(ctx.tmux, session, r"Transcript search.*2 items", "active transcript search resize-back refresh")
    send_keys(ctx.tmux, session, "Escape")
    restored = wait_for_screen_state(
        ctx.tmux,
        session,
        lambda screen: "Transcript search" not in screen and "Esc stop" in screen and len(_numbered_lines(screen)) >= 10,
        "active transcript search Esc restoration without canceling the run",
    )
    restored_numbers = _numbered_lines(restored)
    if not paused_numbers or not set(paused_numbers).intersection(restored_numbers):
        raise RuntimeError(
            "Esc did not return to the pre-search active transcript region\n"
            f"before={paused_numbers}\nafter={restored_numbers}\nscreen:\n{restored}"
        )

    send_literal(ctx.tmux, session, "/search line 059")
    send_keys(ctx.tmux, session, "Enter")
    wait_for_screen_state(
        ctx.tmux,
        session,
        lambda screen: "Transcript search" in screen and "0 items" in screen and "No transcript matches" in screen,
        "active transcript search reopened without future stream match",
    )
    continue_marker = controls / "continue"
    continue_marker.write_text("continue\n", encoding="utf-8")
    continue_marker.chmod(0o600)
    completed_modal = wait_for_screen_state(
        ctx.tmux,
        session,
        lambda screen: "Transcript search" in screen and "1 item" in screen and "filter  line 059" in screen and bool(_modal_selected_row(screen)),
        "streaming transcript search refresh and idle transition",
        timeout=10.0,
    )
    save_evidence(ctx.root, "transcript-search-streaming-refresh", completed_modal)
    send_keys(ctx.tmux, session, "Enter")
    jumped_newest = wait_for_screen_state(
        ctx.tmux,
        session,
        lambda screen: "Transcript search" not in screen and "stream line 000" in screen,
        "transcript search Enter item jump",
    )

    send_literal(ctx.tmux, session, "/search Ä")
    send_keys(ctx.tmux, session, "Enter")
    unicode_search = wait_for_screen_state(
        ctx.tmux,
        session,
        lambda screen: "Transcript search" in screen and "1 item" in screen and "user" in _modal_selected_row(screen),
        "idle Unicode transcript search",
    )
    send_keys(ctx.tmux, session, "Escape")
    wait_for_screen_state(
        ctx.tmux,
        session,
        lambda screen: "Transcript search" not in screen and "stream line 000" in screen,
        "idle Unicode search cancellation and viewport restoration",
    )

    send_literal(ctx.tmux, session, "/search line 005")
    send_keys(ctx.tmux, session, "Enter")
    mouse_search = wait_for_screen_state(
        ctx.tmux,
        session,
        lambda screen: "Transcript search" in screen and "1 item" in screen and bool(_modal_selected_row(screen)),
        "idle transcript search mouse result",
    )
    _click_selected_row(ctx, session, mouse_search)
    jumped_old = wait_for_screen_state(
        ctx.tmux,
        session,
        lambda screen: "Transcript search" not in screen and "stream line 000" in screen,
        "transcript search mouse item jump",
    )
    save_evidence(ctx.root, "transcript-search-idle-jump", jumped_old)

    send_keys(ctx.tmux, session, "C-d")
    wait_for_session_exit(ctx.tmux, session)
    provider.stop()
    provider_error = provider.stderr_path.read_text(encoding="utf-8", errors="replace")
    if provider_error:
        raise RuntimeError(f"transcript-search fake provider reported an error:\n{provider_error}")
