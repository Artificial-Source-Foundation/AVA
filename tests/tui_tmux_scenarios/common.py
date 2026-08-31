"""Shared support for independent tmux TUI smoke scenarios."""

from __future__ import annotations

import json
import pathlib
import re
import time

from test_timing_trace import timed_operation, timing_poll

from tui_smoke_helpers import (
    POLL_INTERVAL,
    SmokeContext,
    capture,
    send_keys,
    send_literal,
    wait_for,
    wait_for_absent,
    wait_for_screen_change,
    wait_for_session_exit,
)


_TITLE_GENERATION_SYSTEM_PROMPT = (
    "Create one natural 5-10-word conversation title. Return only the title, with no reasoning, quotes, markup, or trailing punctuation."
)



def _request_log_entries(request_log: str) -> list[str]:
    return [entry for entry in request_log.split("--- request ")[1:] if entry]


def _is_title_generation_request(request: str) -> bool:
    _, separator, body = request.partition("\n\n")
    if not separator:
        return False
    try:
        payload = json.loads(body)
    except json.JSONDecodeError:
        return False
    messages = payload.get("messages") if isinstance(payload, dict) else None
    return isinstance(messages, list) and any(
        isinstance(message, dict)
        and message.get("role") == "system"
        and message.get("content") == _TITLE_GENERATION_SYSTEM_PROMPT
        for message in messages
    )


def _request_counts(request_log: str) -> tuple[int, int]:
    requests = _request_log_entries(request_log)
    return sum(not _is_title_generation_request(request) for request in requests), len(requests)


def _request_count_diagnostic(request_log: str) -> str:
    turns, total = _request_counts(request_log)
    return f"normal conversation turns={turns}; total provider requests={total}"


@timed_operation("provider_wait", label_argument="label")
def _wait_for_normal_turn_request_count(path: pathlib.Path, expected_count: int, label: str, timeout: float = 8.0) -> str:
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        timing_poll()
        if path.exists():
            last = path.read_text(encoding="utf-8", errors="replace")
            turns, _ = _request_counts(last)
            if turns >= expected_count:
                return last
        time.sleep(POLL_INTERVAL)
    raise RuntimeError(
        f"timed out waiting for {label}; expected at least {expected_count} normal conversation turns; "
        f"{_request_count_diagnostic(last)}\nrequest log:\n{last}"
    )


@timed_operation("observation", label_argument="label")
def _assert_normal_turn_request_count_stays(path: pathlib.Path, expected_count: int, label: str, duration: float = 1.2) -> str:
    deadline = time.monotonic() + duration
    last = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    while time.monotonic() < deadline:
        timing_poll()
        if path.exists():
            last = path.read_text(encoding="utf-8", errors="replace")
        turns, _ = _request_counts(last)
        if turns != expected_count:
            raise RuntimeError(
                f"{label}; expected exactly {expected_count} normal conversation turns, saw {turns}; "
                f"{_request_count_diagnostic(last)}\nrequest log:\n{last}"
            )
        time.sleep(POLL_INTERVAL)
    return last


def _main_session(ctx: SmokeContext) -> tuple[object, pathlib.Path, pathlib.Path, pathlib.Path, str, str]:
    """Launch a fresh isolated main TUI used by one main_* scenario."""

    tmux_exe = ctx.tmux
    root = ctx.root
    workspace = ctx.workspace
    ava_config = ctx.ava_config
    env_prefix = ctx.main_pane_command()
    session = ctx.session_name("main")
    ava_config.joinpath("keybinds.json").write_text(
        '{"tui.editor.cursorLineStart":["Home","Ctrl+A"]}\n', encoding="utf-8"
    )
    ctx.launch_ava(session, workspace=workspace, command=env_prefix)
    wait_for(tmux_exe, session, r"Type a message|live session", "main scenario initial TUI frame")
    return tmux_exe, root, workspace, ava_config, env_prefix, session


def _finish_main(tmux_exe: object, session: str) -> None:
    # Local command output is modal by design; close a final report before
    # sending the ordinary idle exit key, but do not arm idle Escape handling.
    if re.search(r"Command /|Command !", capture(tmux_exe, session)):
        send_keys(tmux_exe, session, "Escape")
        wait_for_absent(tmux_exe, session, r"Command /|Command !", "final local command output closed")
    send_keys(tmux_exe, session, "C-d")
    wait_for_session_exit(tmux_exe, session)


# Match settings while open with either empty or active compact search chrome.
_SETTINGS_OPEN_PATTERN = r"Settings ›|Search\s{2}\S|Enter open · Esc close|Enter select · Esc back"
# Root-only markers: section titles always include the breadcrumb separator.
_SETTINGS_ROOT_PATTERN = r"(?s)Settings.*Theme.*Display.*Model.*Input.*Workspace.*Tools"
# A filtered root can show only one section, but its standalone heading never
# contains the nested-section breadcrumb separator.
_SETTINGS_ROOT_FRAME_PATTERN = r"(?m)^\s*Settings\s*$"
# Composer rows only: reject transcript/modal copy that mentions the same text.
_COMPOSER_EMPTY_PATTERN = r"(?m)^\s*│\s+Type a message\.\.\."
_COMPOSER_SETTINGS_DRAFT_PATTERN = r"(?m)^\s*│\s+/settings(?:\s|$)"


def wait_for_settings_root(tmux_exe: object, session: str, label: str, timeout: float = 8.0) -> str:
    """Wait for the standalone root heading rather than text shared by a nested section."""

    return wait_for(tmux_exe, session, _SETTINGS_ROOT_FRAME_PATTERN, label, timeout=timeout)


def _ensure_empty_composer(tmux_exe: object, session: str, label: str, timeout: float = 8.0) -> str:
    """Clear any composer draft and synchronize on the visible empty composer row.

    A prior Escape used to dismiss settings can swallow the next clear key under load,
    so retry from visible state instead of bursting Escape/C-u/literal immediately.
    """

    deadline = time.monotonic() + timeout
    last = capture(tmux_exe, session)
    while time.monotonic() < deadline:
        if re.search(_SETTINGS_OPEN_PATTERN, last):
            close_settings(tmux_exe, session, f"{label} pre-close")
            last = capture(tmux_exe, session)
            continue
        if re.search(_COMPOSER_EMPTY_PATTERN, last):
            return last
        send_keys(tmux_exe, session, "C-u")
        remaining = max(0.1, deadline - time.monotonic())
        try:
            last = wait_for(
                tmux_exe,
                session,
                _COMPOSER_EMPTY_PATTERN,
                f"{label} empty composer",
                timeout=min(2.0, remaining),
            )
            if not re.search(_SETTINGS_OPEN_PATTERN, last):
                return last
        except RuntimeError:
            last = capture(tmux_exe, session)
    raise RuntimeError(f"{label}; composer did not become empty before /settings\nscreen:\n{last}")


def open_settings_root(tmux_exe: object, session: str, label: str = "settings root") -> str:
    """Open `/settings` to the shallow root section list."""

    # Ensure any previous settings frame is gone and the composer is idle before drafting.
    _ensure_empty_composer(tmux_exe, session, label)
    send_literal(tmux_exe, session, "/settings")
    wait_for(tmux_exe, session, _COMPOSER_SETTINGS_DRAFT_PATTERN, f"{label} command draft")
    before = capture(tmux_exe, session)
    send_keys(tmux_exe, session, "Enter")
    wait_for_screen_change(tmux_exe, session, before, f"{label} open redraw")
    return wait_for(tmux_exe, session, _SETTINGS_ROOT_PATTERN, label)


def open_settings_section(
    tmux_exe: object,
    session: str,
    section_query: str,
    section_marker: str,
    label: str,
) -> str:
    """Open `/settings`, filter the root to one section, and Enter into it."""

    open_settings_root(tmux_exe, session, f"{label} root")
    send_literal(tmux_exe, session, section_query)
    query_pattern = re.escape(section_query)
    if len(section_query) > 1:
        query_pattern = rf"(?:{query_pattern}|{re.escape(section_query[1:])})"
    wait_for(tmux_exe, session, rf"Search\s{{2}}{query_pattern}", f"{label} section filter")
    # Jump to the best/first match. Weak fuzzy hits can leave the previous root row
    # selected (for example Display's value settings:section.display matching "Sessions").
    send_keys(tmux_exe, session, "Home")
    before = capture(tmux_exe, session)
    send_keys(tmux_exe, session, "Enter")
    # Wait for the section frame itself before typing nested filters so the first
    # filter character is not lost to the still-closing root key handler.
    wait_for_screen_change(tmux_exe, session, before, f"{label} section open redraw")
    return wait_for(tmux_exe, session, section_marker, label)


def clear_settings_filter(tmux_exe: object, session: str, label: str = "settings filter cleared") -> str:
    """Clear the current section filter with Backspace (Ctrl+U is a composer-only draft clear)."""

    before = capture(tmux_exe, session)
    # Select-list filters are short; bound the clear so a stuck modal cannot hang the smoke.
    for _ in range(48):
        screen = capture(tmux_exe, session)
        if not re.search(r"Search\s{2}\S", screen):
            return screen
        send_keys(tmux_exe, session, "BSpace")
    after = capture(tmux_exe, session)
    if after == before or re.search(r"Search\s{2}\S", after):
        raise RuntimeError(f"{label}; filter did not clear\nscreen:\n{after}")
    return after


def close_settings(tmux_exe: object, session: str, label: str = "settings closed", close_key: str = "Escape") -> None:
    """Send ``close_key`` until the settings selector is gone, including its section back-stack."""

    # Wave 2 keeps at most root + one section frame. Prefer Escape only: Ctrl+C can be
    # delivered as SIGINT depending on the terminal path and is not reliable here.
    for attempt in range(4):
        screen = capture(tmux_exe, session)
        if not re.search(_SETTINGS_OPEN_PATTERN, screen):
            return
        before = screen
        send_keys(tmux_exe, session, close_key)
        try:
            # A normal bare Escape settles after AVA's 100 ms ambiguity window.
            # Retry after 500 ms instead of turning an ignored key into a two-second stall.
            wait_for_screen_change(tmux_exe, session, before, f"{label} esc {attempt + 1}", timeout=0.5)
        except RuntimeError:
            # Already closed or visually identical root/composer transition.
            pass
    final = capture(tmux_exe, session)
    if re.search(_SETTINGS_OPEN_PATTERN, final):
        raise RuntimeError(f"{label}; settings still open\nscreen:\n{final}")


def assert_title_first_new_receipt(screen: str, created_title: str, previous_title: str, label: str) -> str:
    """Return the newest title-matching /new receipt after checking its complete three-line shape."""

    created = re.escape(created_title)
    previous = re.escape(previous_title)
    main_pane_screen = "\n".join(line.partition("│")[0].rstrip() for line in screen.splitlines())
    receipt_pattern = re.compile(
        rf'(?m)^(?P<started>[^\n]*started session "{created}" · id\s+(?P<created_id>session_[^\s]+)[^\n]*)\n'
        rf'(?P<previous>[^\n]*previous session "{previous}" · id\s+(?P<previous_id>session_[^\s]+)[^\n]*)\n'
        rf'(?P<switched>[^\n]*switched to "{created}"[^\n]*)$'
    )
    matches = list(receipt_pattern.finditer(main_pane_screen))
    if not matches:
        raise RuntimeError(
            f"{label} did not emit a title-first current-session lifecycle receipt\n"
            f"screen:\n{screen}"
        )

    receipt = matches[-1]
    receipt_text = receipt.group(0)
    created_id = receipt.group("created_id")
    previous_id = receipt.group("previous_id")
    if receipt_text.count(created_id) != 1 or receipt_text.count(previous_id) != 1:
        raise RuntimeError(
            f"{label} did not emit each created and previous session id exactly once\n"
            f"receipt:\n{receipt_text}\nscreen:\n{screen}"
        )
    if "session_" in receipt.group("switched"):
        raise RuntimeError(
            f"{label} emitted a session id in its complete switched lifecycle line\n"
            f"receipt:\n{receipt_text}\nscreen:\n{screen}"
        )
    return receipt_text
