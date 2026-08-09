"""Credential-free real SGR transcript selection/copy coverage."""

from __future__ import annotations

import base64
import binascii
import re
import shlex
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
    wait_for_screen_state,
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


def _wait_for_no_reverse(ctx: SmokeContext, session: str, label: str, timeout: float = 8.0) -> str:
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        last = capture_styled(ctx.tmux, session)
        if "\x1b[7m" not in last:
            return last
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {label}\nstyled screen:\n{last!r}")


def _send_sgr_clicks(ctx: SmokeContext, session: str, column: int, row: int, count: int) -> None:
    # One literal write keeps each physical press/release chain comfortably
    # inside AVA's multi-click interval without introducing timing sleeps.
    send_literal(ctx.tmux, session, (f"\x1b[<0;{column};{row}M\x1b[<0;{column};{row}m") * count)


def _copy_selection_payload(ctx: SmokeContext, session: str, name: str, expected: bytes) -> str:
    # Selection copy shares the terminal clipboard's existing 64 KiB limit. The
    # small fixture must produce one complete direct request followed by one
    # complete tmux DCS passthrough request. This proves only the emitted request
    # bytes, not downstream delivery to a desktop clipboard.
    max_clipboard_bytes = 64 * 1024
    if not expected or len(expected) > max_clipboard_bytes:
        raise RuntimeError(f"invalid bounded OSC52 smoke expectation: {len(expected)} bytes")

    encoded = base64.b64encode(expected)
    raw_request = b"\x1b]52;c;" + encoded + b"\x1b\\"
    tmux_request = b"\x1bPtmux;" + raw_request.replace(b"\x1b", b"\x1b\x1b") + b"\x1b\\"
    complete_transport = raw_request + tmux_request

    pane_output = ctx.root / f"{name}-pane-output.bin"
    pane_output.unlink(missing_ok=True)
    tmux(ctx.tmux, "pipe-pane", "-t", session, f"cat > {shlex.quote(str(pane_output))}")
    try:
        send_keys(ctx.tmux, session, "C-c")
        deadline = time.monotonic() + 8.0
        last = b""
        while time.monotonic() < deadline:
            if pane_output.exists():
                last = pane_output.read_bytes()
                if complete_transport in last:
                    break
            time.sleep(0.05)
        else:
            raise RuntimeError(
                f"timed out waiting for {name}; pane output did not contain the complete raw-plus-tmux OSC52 transport: {last!r}"
            )

        try:
            decoded = base64.b64decode(encoded, validate=True)
        except (ValueError, binascii.Error) as error:
            raise RuntimeError(f"{name} OSC52 request payload was not valid base64: {encoded!r}") from error
        if decoded != expected or len(decoded) > max_clipboard_bytes:
            raise RuntimeError(f"{name} emitted the wrong bounded OSC52 request: decoded={decoded!r}, expected={expected!r}")
        if last.count(complete_transport) != 1 or last.count(tmux_request) != 1:
            raise RuntimeError(
                f"{name} did not emit one exact raw-plus-complete-tmux OSC52 transport; pane output={last!r}"
            )
    finally:
        tmux(ctx.tmux, "pipe-pane", "-t", session, check=False)

    return wait_for(ctx.tmux, session, r"selection copy request sent", f"{name} truthful request-sent status")


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
    logical_line = "TRANSCRIPT MULTICLICK TARGETWORD SENTINEL"
    target_word = "TARGETWORD"
    ctx.active_workspace.joinpath("transcript-selection.txt").write_text(logical_line + "\n", encoding="utf-8")
    ctx.launch_ava(session, workspace=ctx.active_workspace, command=command, width=110, height=28)
    wait_for(ctx.tmux, session, r"Type a message|live session", "transcript-selection initial frame")

    send_literal(ctx.tmux, session, "credential-free transcript selection seed")
    send_keys(ctx.tmux, session, "Enter")
    _wait_for_normal_turn_request_count(provider.request_log, 1, "transcript-selection fake-provider request")
    wait_for(ctx.tmux, session, r"headless active prompt complete", "transcript-selection fake-provider response")

    send_literal(ctx.tmux, session, "/details compact")
    send_keys(ctx.tmux, session, "Enter")
    wait_for(ctx.tmux, session, r"tool details are now compact", "transcript-selection compact tool-card mode")

    send_literal(ctx.tmux, session, "/read transcript-selection.txt")
    send_keys(ctx.tmux, session, "Enter")
    card = wait_for(
        ctx.tmux,
        session,
        rf"{re.escape(logical_line)}|Permission required",
        "transcript-selection tool-card seed",
    )
    if "Permission required" in card:
        send_keys(ctx.tmux, session, "Tab", "Enter")
        card = wait_for(
            ctx.tmux,
            session,
            re.escape(logical_line),
            "transcript-selection allowed tool card",
        )

    header_pattern = r"[Rr]ead.*transcript-selection\.txt"
    header_row, header_line = _last_row_matching(card, header_pattern)
    body_row, body_line = _last_row_matching(card, re.escape(logical_line))
    if body_row <= header_row:
        raise RuntimeError(
            "tool-card body was not below its header, so header drag geometry was not testable\n"
            f"header row {header_row}: {header_line!r}\nbody row {body_row}: {body_line!r}\nscreen:\n{card}"
        )
    header_column = max(1, len(header_line) - len(header_line.lstrip()) + 2)
    body_column = body_line.index(target_word) + 1 + 2

    # Rapid ordinary header clicks remain actions rather than body multi-click
    # seeds. Re-read geometry after each redraw because expansion changes the
    # tmux pane rows, and synchronize on visible detail content.
    rapid_click_interval = 0.5
    rapid_started = time.monotonic()
    _send_sgr_clicks(ctx, session, header_column, header_row, 1)
    expanded = wait_for_screen_state(
        ctx.tmux,
        session,
        lambda screen: "output:" in screen,
        "transcript-selection header expanded state",
        timeout=0.35,
    )
    if "\x1b[7m" in capture_styled(ctx.tmux, session):
        raise RuntimeError(f"header click started transcript selection while expanding\nscreen:\n{expanded}")
    expanded_header_row, expanded_header_line = _last_row_matching(expanded, header_pattern)
    expanded_header_column = max(1, len(expanded_header_line) - len(expanded_header_line.lstrip()) + 2)
    _send_sgr_clicks(ctx, session, expanded_header_column, expanded_header_row, 1)
    rapid_elapsed = time.monotonic() - rapid_started
    if rapid_elapsed >= rapid_click_interval:
        raise RuntimeError(
            f"header click chain took {rapid_elapsed:.3f}s, so it did not exercise AVA's 500 ms multi-click interval"
        )
    collapsed = wait_for_screen_state(
        ctx.tmux,
        session,
        lambda screen: "output:" not in screen,
        "transcript-selection header collapsed state",
    )
    if "output:" in collapsed or "\x1b[7m" in capture_styled(ctx.tmux, session):
        raise RuntimeError(f"rapid header click was hijacked by word/line selection instead of collapsing\nscreen:\n{collapsed}")

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

    send_keys(ctx.tmux, session, "Escape")
    _wait_for_no_reverse(ctx, session, "transcript-selection cleared drag selection")

    # Exercise physical multi-click press/release reports on one stable body
    # cell and verify the real clipboard bytes, not only reverse-video paint.
    _send_sgr_clicks(ctx, session, body_column, body_row, 2)
    _wait_for_reverse(ctx, session, "transcript-selection double-click word")
    _copy_selection_payload(ctx, session, "transcript-selection-double-click", target_word.encode("utf-8"))

    send_keys(ctx.tmux, session, "Escape")
    _wait_for_no_reverse(ctx, session, "transcript-selection cleared double-click selection")
    _send_sgr_clicks(ctx, session, body_column, body_row, 3)
    _wait_for_reverse(ctx, session, "transcript-selection triple-click logical line")
    # The transcript projection owns its two-space body indent, so selecting
    # the complete logical rendered line includes it in the clipboard text.
    triple_copied = _copy_selection_payload(
        ctx,
        session,
        "transcript-selection-triple-click",
        f"  {logical_line}".encode("utf-8"),
    )
    save_evidence(ctx.root, "transcript-selection-multiclick-copied", triple_copied)

    send_keys(ctx.tmux, session, "C-d")
    wait_for_session_exit(ctx.tmux, session)
