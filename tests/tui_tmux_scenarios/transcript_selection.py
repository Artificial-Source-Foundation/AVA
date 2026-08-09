"""Credential-free real SGR transcript drag/copy coverage."""

from __future__ import annotations

import re
import time

from tui_smoke_helpers import (
    SmokeContext,
    capture,
    capture_styled,
    save_evidence,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_session_exit,
)
from .common import _wait_for_normal_turn_request_count


def _last_row_matching(screen: str, pattern: str) -> tuple[int, str]:
    matches = [(row, line) for row, line in enumerate(screen.splitlines(), start=1) if re.search(pattern, line)]
    if not matches:
        raise RuntimeError(f"could not locate transcript row matching {pattern!r}\nscreen:\n{screen}")
    return matches[-1]


def _wait_for_reverse(ctx: SmokeContext, session: str, label: str, timeout: float = 8.0) -> str:
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        last = capture_styled(ctx.tmux, session)
        if "\x1b[7m" in last:
            return last
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {label}\nstyled screen:\n{last!r}")


def scenario_transcript_selection(ctx: SmokeContext) -> None:
    session = ctx.session_name("transcript-selection")
    provider = ctx.start_fake_provider("transcript-selection", delay_ms=0)
    command = ctx.fake_provider_command(
        provider,
        home=ctx.active_home,
        config=ctx.active_config,
        state=ctx.active_state,
        data=ctx.active_data,
        no_color=False,
    )
    ctx.active_workspace.joinpath("transcript-selection.txt").write_text(
        "TRANSCRIPT-SELECTION-CARD-CONTENT\n", encoding="utf-8"
    )
    ctx.launch_ava(session, workspace=ctx.active_workspace, command=command, width=110, height=28)
    wait_for(ctx.tmux, session, r"Type a message|live session", "transcript-selection initial frame")

    send_literal(ctx.tmux, session, "credential-free transcript selection seed")
    send_keys(ctx.tmux, session, "Enter")
    _wait_for_normal_turn_request_count(provider.request_log, 1, "transcript-selection fake-provider request")
    wait_for(ctx.tmux, session, r"headless active prompt complete", "transcript-selection fake-provider response")

    send_literal(ctx.tmux, session, "/read transcript-selection.txt")
    send_keys(ctx.tmux, session, "Enter")
    card = wait_for(
        ctx.tmux,
        session,
        r"TRANSCRIPT-SELECTION-CARD-CONTENT|Permission required",
        "transcript-selection tool-card seed",
    )
    if "Permission required" in card:
        send_keys(ctx.tmux, session, "Tab", "Enter")
        card = wait_for(
            ctx.tmux,
            session,
            r"TRANSCRIPT-SELECTION-CARD-CONTENT",
            "transcript-selection allowed tool card",
        )

    header_row, header_line = _last_row_matching(card, r"[Rr]ead.*transcript-selection\.txt")
    body_row, body_line = _last_row_matching(card, r"TRANSCRIPT-SELECTION-CARD-CONTENT")
    if body_row <= header_row:
        raise RuntimeError(
            "tool-card body was not below its header, so header drag geometry was not testable\n"
            f"header row {header_row}: {header_line!r}\nbody row {body_row}: {body_line!r}\nscreen:\n{card}"
        )
    header_column = max(1, len(header_line) - len(header_line.lstrip()) + 2)
    body_column = max(header_column + 4, len(body_line) - len(body_line.lstrip()) + 18)

    # Send actual xterm SGR press, motion, and release reports through tmux. The
    # first endpoint is a toggle-capable tool header; movement must turn it into
    # a selection anchored at that original press rather than toggling the card.
    mouse_mode = tmux(
        ctx.tmux,
        "display-message",
        "-p",
        "-t",
        session,
        "#{mouse_any_flag} #{mouse_sgr_flag} #{pane_in_mode}",
    ).stdout.strip()
    if not mouse_mode.startswith("1 1 "):
        raise RuntimeError(f"AVA did not enable button-motion SGR mouse reporting in tmux: {mouse_mode}")
    send_literal(ctx.tmux, session, f"\x1b[<0;{header_column};{header_row}M")
    send_literal(ctx.tmux, session, f"\x1b[<32;{body_column};{body_row}M")
    _wait_for_reverse(ctx, session, "transcript-selection synchronized real SGR press/drag")
    send_literal(ctx.tmux, session, f"\x1b[<0;{body_column};{body_row}m")
    selected = capture(ctx.tmux, session)
    if "tool details expanded" in selected or "tool details collapsed" in selected:
        raise RuntimeError(f"header drag toggled the tool card instead of selecting\nscreen:\n{selected}")

    send_keys(ctx.tmux, session, "C-c")
    copied = wait_for(ctx.tmux, session, r"selection copy request sent", "transcript-selection CopySelection status")
    copied_styled = capture_styled(ctx.tmux, session)
    if "tool details expanded" in copied or "tool details collapsed" in copied:
        raise RuntimeError(f"CopySelection surfaced a header toggle status\nscreen:\n{copied}")
    if "\x1b[7m" not in copied_styled:
        raise RuntimeError(f"successful CopySelection cleared the transcript highlight\nstyled screen:\n{copied_styled!r}")
    save_evidence(ctx.root, "transcript-selection-copied", capture(ctx.tmux, session))

    send_keys(ctx.tmux, session, "C-d")
    wait_for_session_exit(ctx.tmux, session)
