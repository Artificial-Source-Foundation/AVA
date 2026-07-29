"""Real-terminal streaming, detached scrollback, and input responsiveness coverage."""

from __future__ import annotations

import pathlib
import re
import time

from tui_smoke_helpers import (
    SmokeContext,
    capture,
    save_evidence,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_absent,
    wait_for_screen_state,
    wait_for_session_exit,
)
from .common import _wait_for_normal_turn_request_count


_NUMBERED_LINE = re.compile(r"stream line (\d{3})")
_DELETED_SCROLLBACK_TEXT = ("scrollback detached", "updates below", "jump_to_bottom")


def _wait_for_path(path: pathlib.Path, label: str, timeout: float = 10.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {label}: {path}")


def _wait_for_session_text(state_root: pathlib.Path, needle: str, label: str, timeout: float = 10.0) -> pathlib.Path:
    deadline = time.monotonic() + timeout
    seen: list[pathlib.Path] = []
    while time.monotonic() < deadline:
        seen = list(state_root.rglob("*.jsonl"))
        for path in seen:
            if needle in path.read_text(encoding="utf-8", errors="replace"):
                return path
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {label}; inspected session files: {seen}")


def _numbered_window(screen: str, label: str) -> list[int]:
    numbers = [int(value) for value in _NUMBERED_LINE.findall(screen)]
    if len(numbers) < 10 or numbers != list(range(numbers[0], numbers[-1] + 1)):
        raise RuntimeError(f"{label} did not contain a contiguous numbered stream window\nnumbers: {numbers}\nscreen:\n{screen}")
    return numbers


def _assert_no_deleted_scrollback_text(screen: str, label: str) -> None:
    surfaced = [text for text in _DELETED_SCROLLBACK_TEXT if text in screen]
    if surfaced:
        raise RuntimeError(f"{label} surfaced deleted scrollback chrome {surfaced}\nscreen:\n{screen}")


def scenario_streaming_scroll(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    root = ctx.root
    session = ctx.session_name("streaming-scroll")
    controls = root / "streaming-controls"
    controls.mkdir(mode=0o700)
    controls.chmod(0o700)

    streaming_models = (
        '{"default_provider":"moonshot","default_model":"ava-tui-fake",'
        '"models":[{"provider":"moonshot","id":"ava-tui-fake","name":"AVA TUI Fake","family":"fake",'
        '"context_window_tokens":8192,"max_output_tokens":1024,"supports_tools":false,'
        '"supports_streaming":true,"supports_reasoning":false,"reports_usage":true}]}\n'
    )
    ctx.active_ava_config.joinpath("models.json").write_text(streaming_models, encoding="utf-8")

    provider = ctx.start_fake_provider("streaming-scroll", delay_ms=20, scenario="streaming-scroll", target=controls)
    command = ctx.fake_provider_command(
        provider,
        home=ctx.active_home,
        config=ctx.active_config,
        state=ctx.active_state,
        data=ctx.active_data,
    )
    ctx.launch_ava(session, workspace=ctx.active_workspace, command=command, width=120, height=32)
    wait_for(tmux_exe, session, r"Type a message|live session", "streaming-scroll initial frame")

    send_literal(tmux_exe, session, "exercise deterministic streaming scroll")
    wait_for(tmux_exe, session, r"exercise deterministic streaming scroll", "streaming-scroll prompt draft")
    send_keys(tmux_exe, session, "Enter")
    _wait_for_normal_turn_request_count(provider.request_log, 1, "streaming-scroll provider request")

    draft = "STREAM-DRAFT-RESPONSIVE"
    same_direction_suffix = "-WHEEL-SAME"
    alternating_suffix = "-WHEEL-ALT"
    active_prefix = "A" * 160
    same_direction_draft = draft + same_direction_suffix
    complete_draft = same_direction_draft + alternating_suffix
    active_started = time.monotonic()
    send_literal(tmux_exe, session, active_prefix + draft)
    responsive = wait_for(tmux_exe, session, re.escape(draft), "streaming-scroll responsive active draft", timeout=2.0)
    active_elapsed = time.monotonic() - active_started
    if "STREAM COMPLETE" in responsive:
        raise RuntimeError(f"active draft was not observed before stream completion\nscreen:\n{responsive}")

    _wait_for_path(controls / "paused", "streaming-scroll paused marker")
    initial_screen = wait_for(tmux_exe, session, r"stream line 029", "streaming-scroll initial numbered tail")
    initial_numbers = _numbered_window(initial_screen, "initial paused stream")
    if initial_numbers[-1] != 29 or initial_numbers[0] == 0 or draft not in initial_screen:
        raise RuntimeError(
            "initial paused stream did not expose a movable tail ending at line 029 with the draft intact\n"
            f"numbers: {initial_numbers}\nscreen:\n{initial_screen}"
        )
    _assert_no_deleted_scrollback_text(initial_screen, "initial paused stream")

    wheel_up = "\x1b[<64;4;6M"
    wheel_down = "\x1b[<65;4;6M"
    same_direction_started = time.monotonic()
    send_literal(tmux_exe, session, wheel_up * 12 + same_direction_suffix)
    same_direction_screen = wait_for(
        tmux_exe, session, re.escape(same_direction_draft), "streaming-scroll same-direction wheel burst consumed before suffix", timeout=2.0
    )
    same_direction_elapsed = time.monotonic() - same_direction_started
    same_direction_numbers = _numbered_window(same_direction_screen, "same-direction wheel-burst detached stream")
    same_size_shift = [initial_numbers[0] - 1, *initial_numbers[:-1]]
    one_extra_visible_row = [initial_numbers[0] - 1, *initial_numbers]
    if same_direction_numbers not in (same_size_shift, one_extra_visible_row):
        raise RuntimeError(
            "raw same-direction wheel burst did not produce exactly one upward transcript row\n"
            f"active typing elapsed: {active_elapsed:.3f}s; wheel elapsed: {same_direction_elapsed:.3f}s\n"
            f"before: {initial_numbers}\nafter: {same_direction_numbers}\nscreen:\n{same_direction_screen}"
        )
    if same_direction_draft not in same_direction_screen:
        raise RuntimeError(f"same-direction wheel burst changed the composer draft or cursor insertion point\nscreen:\n{same_direction_screen}")
    _assert_no_deleted_scrollback_text(same_direction_screen, "same-direction wheel-burst detached stream")

    burst_started = time.monotonic()
    send_literal(tmux_exe, session, (wheel_up + wheel_down) * 60 + alternating_suffix)
    burst_screen = wait_for(tmux_exe, session, re.escape(complete_draft), "streaming-scroll alternating wheel flood consumed before suffix", timeout=2.0)
    burst_elapsed = time.monotonic() - burst_started
    burst_numbers = _numbered_window(burst_screen, "alternating wheel-flood stream")
    if burst_numbers[0] < 0 or burst_numbers[-1] > initial_numbers[-1]:
        raise RuntimeError(
            "alternating wheel flood escaped the bounded paused transcript window\n"
            f"active typing elapsed: {active_elapsed:.3f}s; same-direction wheel elapsed: {same_direction_elapsed:.3f}s; "
            f"alternating wheel elapsed: {burst_elapsed:.3f}s\n"
            f"before: {same_direction_numbers}\nafter: {burst_numbers}\nscreen:\n{burst_screen}"
        )
    if complete_draft not in burst_screen:
        raise RuntimeError(f"alternating wheel flood changed the composer draft or cursor insertion point\nscreen:\n{burst_screen}")
    _assert_no_deleted_scrollback_text(burst_screen, "alternating wheel-flood stream")

    continue_marker = controls / "continue"
    continue_marker.write_text("continue\n", encoding="utf-8")
    continue_marker.chmod(0o600)
    _wait_for_path(controls / "completed", "streaming-scroll completed marker")
    _wait_for_session_text(ctx.active_state, "STREAM COMPLETE", "persisted streaming completion")

    completed_detached = capture(tmux_exe, session)
    completed_detached_numbers = _numbered_window(completed_detached, "completed detached stream")
    if completed_detached_numbers[: len(burst_numbers)] != burst_numbers or complete_draft not in completed_detached:
        raise RuntimeError(
            "detached transcript or draft moved while the stream completed below\n"
            f"before: {burst_numbers}\nafter: {completed_detached_numbers}\nscreen:\n{completed_detached}"
        )
    _assert_no_deleted_scrollback_text(completed_detached, "completed detached stream")

    send_literal(tmux_exe, session, "\x1b[1;5F")

    def restored_live_tail(screen: str) -> bool:
        numbers = [int(value) for value in _NUMBERED_LINE.findall(screen)]
        return "STREAM COMPLETE" in screen and complete_draft in screen and bool(numbers) and numbers[-1] == 59

    ctrl_end_live = wait_for_screen_state(
        tmux_exe,
        session,
        restored_live_tail,
        "streaming-scroll Ctrl+End returns detached stream to live tail without draft mutation",
    )
    ctrl_end_numbers = _numbered_window(ctrl_end_live, "Ctrl+End restored live tail")
    if ctrl_end_numbers[-1] != 59 or complete_draft not in ctrl_end_live:
        raise RuntimeError(
            "Ctrl+End did not restore the completed live tail with the composer draft intact\n"
            f"numbers: {ctrl_end_numbers}\nscreen:\n{ctrl_end_live}"
        )
    _assert_no_deleted_scrollback_text(ctrl_end_live, "Ctrl+End restored live tail")

    # Leave live-tail chrome so ordinary Up/Down can be measured as the keyboard scroll step
    # (3 transcript rows) without colliding with live-tail chrome or the oldest boundary.
    keyboard_scroll_rows = 3
    send_keys(tmux_exe, session, *(["Up"] * 4))
    detached_numbered = wait_for_screen_state(
        tmux_exe,
        session,
        lambda screen: complete_draft in screen
        and "STREAM COMPLETE" not in screen
        and (numbers := [int(value) for value in _NUMBERED_LINE.findall(screen)])
        and len(numbers) >= 10
        and numbers[0] >= keyboard_scroll_rows
        and numbers[-1] <= 59 - keyboard_scroll_rows,
        "streaming-scroll detached numbered region before plain arrow scroll checks",
    )
    before_up = _numbered_window(detached_numbered, "detached numbered region before plain Up")

    def keyboard_step_up(screen: str) -> bool:
        numbers = [int(value) for value in _NUMBERED_LINE.findall(screen)]
        if len(numbers) < 10 or complete_draft not in screen:
            return False
        same_size_shift = list(range(before_up[0] - keyboard_scroll_rows, before_up[0] - keyboard_scroll_rows + len(before_up)))
        return numbers == same_size_shift

    send_keys(tmux_exe, session, "Up")
    up_scrolled = wait_for_screen_state(
        tmux_exe,
        session,
        keyboard_step_up,
        "streaming-scroll plain Up scrolls transcript by the keyboard step without draft mutation",
    )
    up_numbers = _numbered_window(up_scrolled, "plain Up keyboard-step scroll")
    if complete_draft not in up_scrolled:
        raise RuntimeError(f"plain Up altered the composer draft\nscreen:\n{up_scrolled}")
    _assert_no_deleted_scrollback_text(up_scrolled, "plain Up keyboard-step scroll")

    before_down = up_numbers

    def keyboard_step_down(screen: str) -> bool:
        numbers = [int(value) for value in _NUMBERED_LINE.findall(screen)]
        if complete_draft not in screen or len(numbers) < 10:
            return False
        same_size_shift = list(range(before_down[0] + keyboard_scroll_rows, before_down[0] + keyboard_scroll_rows + len(before_down)))
        return numbers == same_size_shift

    send_keys(tmux_exe, session, "Down")
    down_scrolled = wait_for_screen_state(
        tmux_exe,
        session,
        keyboard_step_down,
        "streaming-scroll plain Down scrolls transcript by the keyboard step without draft mutation",
    )
    down_numbers = _numbered_window(down_scrolled, "plain Down keyboard-step scroll")
    if complete_draft not in down_scrolled:
        raise RuntimeError(f"plain Down altered the composer draft\nscreen:\n{down_scrolled}")
    _assert_no_deleted_scrollback_text(down_scrolled, "plain Down keyboard-step scroll")

    send_keys(tmux_exe, session, "M-k")
    alt_k_screen = wait_for_screen_state(
        tmux_exe,
        session,
        lambda screen: complete_draft in screen
        and (
            "previous message" in screen
            or "oldest message visible" in screen
            or [int(value) for value in _NUMBERED_LINE.findall(screen)] != down_numbers
        ),
        "streaming-scroll Alt+K moves to a prior message boundary",
    )
    alt_k_numbers = [int(value) for value in _NUMBERED_LINE.findall(alt_k_screen)]
    if complete_draft not in alt_k_screen:
        raise RuntimeError(f"Alt+K altered the composer draft\nscreen:\n{alt_k_screen}")
    if (
        not alt_k_numbers
        and "oldest message visible" not in alt_k_screen
        and "previous message" not in alt_k_screen
    ):
        raise RuntimeError(f"Alt+K did not move message boundaries or report a boundary status\nscreen:\n{alt_k_screen}")
    _assert_no_deleted_scrollback_text(alt_k_screen, "Alt+K prior message boundary")

    send_keys(tmux_exe, session, "M-j")
    alt_j_screen = wait_for_screen_state(
        tmux_exe,
        session,
        lambda screen: complete_draft in screen
        and (
            "next message" in screen
            or "live tail" in screen
            or "STREAM COMPLETE" in screen
            or [int(value) for value in _NUMBERED_LINE.findall(screen)] != alt_k_numbers
        ),
        "streaming-scroll Alt+J moves to the next message boundary or live tail",
    )
    if complete_draft not in alt_j_screen:
        raise RuntimeError(f"Alt+J altered the composer draft\nscreen:\n{alt_j_screen}")
    _assert_no_deleted_scrollback_text(alt_j_screen, "Alt+J next message boundary")

    send_literal(tmux_exe, session, "\x1b[1;5F")
    live_after_nav = wait_for_screen_state(
        tmux_exe,
        session,
        restored_live_tail,
        "streaming-scroll Ctrl+End after message-boundary navigation restores live tail",
    )
    if complete_draft not in live_after_nav:
        raise RuntimeError(f"Ctrl+End after message navigation altered the composer draft\nscreen:\n{live_after_nav}")
    _assert_no_deleted_scrollback_text(live_after_nav, "Ctrl+End after message-boundary navigation")

    live_stream_screen = live_after_nav
    if complete_draft not in live_stream_screen or "STREAM COMPLETE" not in live_stream_screen:
        raise RuntimeError(f"navigation path did not finish on the completed live tail\nscreen:\n{live_stream_screen}")
    _assert_no_deleted_scrollback_text(live_stream_screen, "restored completed live tail after navigation defaults")

    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, re.escape(complete_draft), "streaming-scroll active draft clear")

    idle_draft = "I" * 72 + "IDLE-RESPONSIVE"
    final_draft = idle_draft + "XFINAL"
    idle_started = time.monotonic()
    send_literal(tmux_exe, session, idle_draft + (wheel_up + wheel_down) * 40 + "XFINAL")
    final_screen = wait_for_screen_state(
        tmux_exe,
        session,
        lambda screen: "stream line 059" in screen
        and final_draft in screen
        and sum(1 for line in screen.splitlines() if "AVA TUI Fake" in line) == 1,
        "streaming-scroll completed live tail and retained idle burst draft",
        timeout=2.0,
    )
    idle_elapsed = time.monotonic() - idle_started
    final_numbers = _numbered_window(final_screen, "streaming-scroll final live tail")
    lines = final_screen.splitlines()
    dimensions = tmux(tmux_exe, "display-message", "-p", "-t", session, "#{pane_width},#{pane_height}").stdout.strip()
    metadata_lines = [line for line in lines if "AVA TUI Fake" in line]
    input_rows = [index for index, line in enumerate(lines) if final_draft in line]
    if final_numbers[-1] != 59 or final_draft not in final_screen:
        raise RuntimeError(
            "idle typed/wheel burst did not retain the completed live tail with the exact draft/cursor result\n"
            f"active typing elapsed: {active_elapsed:.3f}s; active wheel elapsed: {burst_elapsed:.3f}s; idle elapsed: {idle_elapsed:.3f}s\n"
            f"screen:\n{final_screen}"
        )
    if len(metadata_lines) != 1:
        raise RuntimeError(f"streaming turn metadata did not appear exactly once\nmetadata: {metadata_lines}\nscreen:\n{final_screen}")
    if dimensions != "120,32" or len(lines) != 32 or any(len(line) > 120 for line in lines):
        raise RuntimeError(f"streaming final evidence did not retain exact bounded 120x32 dimensions\nscreen:\n{final_screen}")
    if (
        len(input_rows) != 1
        or input_rows[0] < 2
        or lines[input_rows[0] - 2].strip()
        or lines[input_rows[0] - 1].strip() != "│"
    ):
        raise RuntimeError(
            f"streaming final frame did not retain the plain breathing gap and elevated guttered composer row immediately above the input\nscreen:\n{final_screen}"
        )
    if "\x1b" in final_screen or any(ord(character) < 32 and character != "\n" for character in final_screen):
        raise RuntimeError(f"streaming final evidence contained ESC or unexpected C0 controls\nscreen:\n{final_screen}")
    _assert_no_deleted_scrollback_text(final_screen, "streaming final live tail")
    save_evidence(root, "streaming-scroll-final", final_screen)

    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, re.escape(final_draft), "streaming-scroll draft clear")
    send_keys(tmux_exe, session, "C-d")
    wait_for_session_exit(tmux_exe, session)
    provider.stop()
    if provider.process.poll() is None:
        raise RuntimeError("streaming-scroll fake provider remained alive after established cleanup")
    provider_error = provider.stderr_path.read_text(encoding="utf-8", errors="replace")
    if provider_error:
        raise RuntimeError(f"streaming-scroll fake provider reported an error:\n{provider_error}")
