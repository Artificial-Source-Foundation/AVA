"""Shared support for independent tmux TUI smoke scenarios."""

from __future__ import annotations

import json
import pathlib
import re
import time

from tui_smoke_helpers import SmokeContext, send_keys, wait_for, wait_for_session_exit


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


def _wait_for_normal_turn_request_count(path: pathlib.Path, expected_count: int, label: str, timeout: float = 8.0) -> str:
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        if path.exists():
            last = path.read_text(encoding="utf-8", errors="replace")
            turns, _ = _request_counts(last)
            if turns >= expected_count:
                return last
        time.sleep(0.1)
    raise RuntimeError(
        f"timed out waiting for {label}; expected at least {expected_count} normal conversation turns; "
        f"{_request_count_diagnostic(last)}\nrequest log:\n{last}"
    )


def _assert_normal_turn_request_count_stays(path: pathlib.Path, expected_count: int, label: str, duration: float = 1.2) -> str:
    deadline = time.monotonic() + duration
    last = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    while time.monotonic() < deadline:
        if path.exists():
            last = path.read_text(encoding="utf-8", errors="replace")
        turns, _ = _request_counts(last)
        if turns != expected_count:
            raise RuntimeError(
                f"{label}; expected exactly {expected_count} normal conversation turns, saw {turns}; "
                f"{_request_count_diagnostic(last)}\nrequest log:\n{last}"
            )
        time.sleep(0.1)
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
    send_keys(tmux_exe, session, "C-d")
    wait_for_session_exit(tmux_exe, session)


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
