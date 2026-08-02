"""The tmux TUI smoke scenario for active run."""

from __future__ import annotations

from tui_smoke_helpers import (
    SmokeContext,
    save_evidence,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_absent,
    wait_for_session_exit,
    wait_for_screen_state,
)
from .common import (
    _assert_normal_turn_request_count_stays,
    _wait_for_normal_turn_request_count,
)


def scenario_active_run(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    root = ctx.root
    active_workspace = ctx.active_workspace
    active_workspace.joinpath("active-card.txt").write_text(
        "".join(f"ACTIVE-OLD-LINE-{line:02d}\n" for line in range(1, 31)), encoding="utf-8"
    )
    active_session = ctx.session_name("active")
    active_provider = ctx.start_fake_provider("active", delay_ms=12000)
    active_request_log = active_provider.request_log
    active_env_prefix = ctx.fake_provider_command(
        active_provider,
        home=ctx.active_home,
        config=ctx.active_config,
        state=ctx.active_state,
        data=ctx.active_data,
    )
    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        active_session,
        "-x",
        "110",
        "-y",
        "28",
        "-c",
        str(active_workspace),
        active_env_prefix,
    )
    wait_for(tmux_exe, active_session, r"Type a message|live session", "active-run fake-provider initial frame")
    send_literal(tmux_exe, active_session, "/help")
    wait_for(tmux_exe, active_session, r"/help", "active-run scrollback seed draft")
    send_keys(tmux_exe, active_session, "Enter")
    wait_for(
        tmux_exe,
        active_session,
        r"page_up PageUp|model_cycle_forward|details_toggle|tree_fold_or_up|tree_unfold_or_down",
        "active-run scrollback seed output",
    )
    send_literal(tmux_exe, active_session, "/write active-card.txt active card detail")
    send_keys(tmux_exe, active_session, "Enter")
    active_card_seed = wait_for(
        tmux_exe,
        active_session,
        r"wrote 18 bytes|Permission required",
        "active-run tool-card seed",
    )
    if "Permission required" in active_card_seed:
        send_keys(tmux_exe, active_session, "Tab", "Enter")
        active_card_seed = wait_for(tmux_exe, active_session, r"wrote 18 bytes", "allowed active-run tool-card seed")
    if "wrote 18 bytes" not in active_card_seed:
        raise RuntimeError(f"active-run tool-card seed did not complete\nscreen:\n{active_card_seed}")
    wait_for(tmux_exe, active_session, r"changed:.*active-card\.txt", "active-run Rich tool-card seed details")
    send_literal(tmux_exe, active_session, "tmux active first prompt")
    wait_for(tmux_exe, active_session, r"tmux active first prompt", "active-run first prompt draft")
    send_keys(tmux_exe, active_session, "Enter")
    _wait_for_normal_turn_request_count(active_request_log, 1, "active-run first provider request")
    active_empty_hint = wait_for(tmux_exe, active_session, r"Esc stop.*type a follow-up", "F3 active empty contextual hint")
    active_dimensions = tmux(tmux_exe, "display-message", "-p", "-t", active_session, "#{pane_width},#{pane_height}").stdout.strip()
    if active_dimensions != "110,28":
        raise RuntimeError(f"F3 active palette dimensions were {active_dimensions}, expected 110,28")
    active_hint_lines = active_empty_hint.splitlines()
    if len(active_hint_lines) != 28 or any(len(line) > 110 for line in active_hint_lines) or not any(line.startswith("│  Esc stop") for line in active_hint_lines):
        raise RuntimeError(f"F3 active contextual hint did not retain its bounded shared composer gutter\nscreen:\n{active_empty_hint}")
    if "\x1b" in active_empty_hint or any(ord(character) < 32 and character != "\n" for character in active_empty_hint):
        raise RuntimeError(f"F3 active contextual hint contained ESC or unexpected C0 controls\nscreen:\n{active_empty_hint}")
    save_evidence(root, "frontend-f3-active-empty-hint", active_empty_hint)

    send_literal(tmux_exe, active_session, "/details compact")
    wait_for(tmux_exe, active_session, r"/details compact", "active /details compact draft before submit")
    send_keys(tmux_exe, active_session, "Enter")
    compact_active_card = wait_for_screen_state(
        tmux_exe,
        active_session,
        lambda screen: "changed:" not in screen and "/details compact" not in screen,
        "active /details compact local mutation",
    )

    send_literal(tmux_exe, active_session, "/details rich")
    send_keys(tmux_exe, active_session, "Enter")
    rich_active_card = wait_for_screen_state(
        tmux_exe,
        active_session,
        lambda screen: "changed:" in screen and "ACTIVE-OLD-LINE-25" not in screen and "/details rich" not in screen,
        "active /details rich local mutation",
    )

    send_literal(tmux_exe, active_session, "/details expanded")
    send_keys(tmux_exe, active_session, "Enter")
    expanded_active_card = wait_for_screen_state(
        tmux_exe,
        active_session,
        lambda screen: "ACTIVE-OLD-LINE-25" in screen and "/details expanded" not in screen,
        "active /details expanded local mutation",
    )

    send_literal(tmux_exe, active_session, "/details compact")
    send_keys(tmux_exe, active_session, "Enter")
    wait_for_screen_state(
        tmux_exe,
        active_session,
        lambda screen: "changed:" not in screen and "/details compact" not in screen,
        "active tool-card compact reset before per-card toggle",
    )
    send_literal(tmux_exe, active_session, "/tools write")
    send_keys(tmux_exe, active_session, "Enter")
    per_card_expanded = wait_for_screen_state(
        tmux_exe,
        active_session,
        lambda screen: "ACTIVE-OLD-LINE-25" in screen and "/tools write" not in screen,
        "active /tools per-card expansion",
    )
    send_literal(tmux_exe, active_session, "/tool write")
    send_keys(tmux_exe, active_session, "Enter")
    per_card_collapsed = wait_for_screen_state(
        tmux_exe,
        active_session,
        lambda screen: "changed:" not in screen and "/tool write" not in screen,
        "active /tool per-card collapse",
    )
    send_literal(tmux_exe, active_session, "/details rich")
    send_keys(tmux_exe, active_session, "Enter")
    wait_for_screen_state(
        tmux_exe,
        active_session,
        lambda screen: "changed:" in screen and "ACTIVE-OLD-LINE-25" not in screen and "/details rich" not in screen,
        "active tool-card Rich restore",
    )
    _assert_normal_turn_request_count_stays(active_request_log, 1, "active local details commands must not reach the provider")
    save_evidence(root, "active-run-local-tool-card-controls", per_card_expanded)

    send_literal(tmux_exe, active_session, "AGENTS")
    active_draft_hint = wait_for(tmux_exe, active_session, r"Esc stop|queue", "F3 active draft contextual hint")
    send_keys(tmux_exe, active_session, "Tab")
    active_forced_path = wait_for(tmux_exe, active_session, r"AGENTS\.md", "F3 active forced path completion")
    if "AGENTS.md" not in active_forced_path:
        raise RuntimeError(f"F3 active forced Tab did not insert the canonical workspace path\nscreen:\n{active_forced_path}")
    save_evidence(root, "frontend-f3-active-forced-path", active_forced_path)
    send_keys(tmux_exe, active_session, "C-u")

    def click_active_candidate(screen: str, needle: str, label: str) -> None:
        candidate = next(((index + 1, line) for index, line in enumerate(screen.splitlines()) if needle in line), None)
        if candidate is None:
            raise RuntimeError(f"{label} did not expose the visible candidate {needle!r}\nscreen:\n{screen}")
        row, line = candidate
        column = line.index(needle) + 1
        send_literal(tmux_exe, active_session, f"\x1b[<0;{column};{row}M")

    send_literal(tmux_exe, active_session, "/")
    active_slash_palette = wait_for(tmux_exe, active_session, r"│\s+› /help", "active-run slash palette")
    click_active_candidate(active_slash_palette, "/help", "active-run slash mouse palette")
    active_slash_selected = wait_for(tmux_exe, active_session, r"│  /help(?:\s|$)", "active-run slash mouse selection")
    if "│  /help" not in active_slash_selected:
        raise RuntimeError(f"active slash mouse selection did not insert the canonical command\nscreen:\n{active_slash_selected}")
    send_keys(tmux_exe, active_session, "C-u")

    send_literal(tmux_exe, active_session, "review @AG")
    active_reference_palette = wait_for(tmux_exe, active_session, r"│\s+› @AGENTS\.md", "active-run @ reference palette")
    click_active_candidate(active_reference_palette, "@AGENTS.md", "active-run @ reference mouse palette")
    active_reference_selected = wait_for(tmux_exe, active_session, r"review @AGENTS\.md", "active-run @ reference mouse selection")
    if "review @AGENTS.md" not in active_reference_selected:
        raise RuntimeError(f"active @ mouse selection did not insert the canonical reference\nscreen:\n{active_reference_selected}")
    send_keys(tmux_exe, active_session, "C-u")

    send_literal(tmux_exe, active_session, "inspect ./AG")
    active_path_palette = wait_for(tmux_exe, active_session, r"│\s+› \./AGENTS\.md", "active-run normal path palette")
    click_active_candidate(active_path_palette, "AGENTS.md", "active-run normal path mouse palette")
    active_path_selected = wait_for(tmux_exe, active_session, r"inspect \./AGENTS\.md", "active-run normal path mouse selection")
    if "inspect ./AGENTS.md" not in active_path_selected:
        raise RuntimeError(f"active path mouse selection did not insert the canonical path\nscreen:\n{active_path_selected}")
    _assert_normal_turn_request_count_stays(active_request_log, 1, "active palette selections must not queue before cleanup")
    send_keys(tmux_exe, active_session, "C-u")
    send_literal(tmux_exe, active_session, "/share")
    active_disabled_share = wait_for(tmux_exe, active_session, r"│  /share", "active disabled slash draft")
    disabled_share_status = r"command disabled: cloud sharing is deferred"
    send_keys(tmux_exe, active_session, "Tab")
    active_disabled_tab = wait_for(
        tmux_exe, active_session, disabled_share_status, "active disabled slash Tab rejection status"
    )
    if "/share" not in active_disabled_tab:
        raise RuntimeError(f"disabled slash Tab mutated the active draft\nscreen:\n{active_disabled_tab}")
    send_keys(tmux_exe, active_session, "Enter")
    active_disabled_enter = wait_for(
        tmux_exe, active_session, disabled_share_status, "active disabled slash Enter rejection status"
    )
    if "/share" not in active_disabled_enter or any(
        status in active_disabled_enter for status in ("job command complete", "follow-up queued", "steering queued", "commands run between turns")
    ):
        raise RuntimeError(f"disabled slash Enter dispatched command/queue output or mutated the active draft\nscreen:\n{active_disabled_enter}")
    click_active_candidate(active_disabled_enter, "/share", "active disabled slash mouse palette")
    active_disabled_mouse = wait_for(
        tmux_exe, active_session, disabled_share_status, "active disabled slash mouse rejection status"
    )
    if "/share" not in active_disabled_mouse or "commands run between turns" in active_disabled_mouse:
        raise RuntimeError(f"disabled slash mouse click mutated or queued the active draft\nscreen:\n{active_disabled_mouse}")
    _assert_normal_turn_request_count_stays(active_request_log, 1, "disabled active slash acceptance must not queue")
    send_keys(tmux_exe, active_session, "C-u")

    send_literal(tmux_exe, active_session, "/")
    wait_for(tmux_exe, active_session, r"/help|Show commands", "active-run slash palette")
    send_literal(tmux_exe, active_session, "\x1b[1;129B")
    wait_for(tmux_exe, active_session, r"› /hotkeys|> /hotkeys", "active-run physical Ghostty arrow palette navigation")
    send_keys(tmux_exe, active_session, "C-u")
    wait_for_absent(tmux_exe, active_session, r"› /hotkeys|> /hotkeys", "active-run slash palette cleared")
    send_literal(tmux_exe, active_session, "\x1b[200~tmux active follow-up\nsecond line\x1b[201~")
    active_live_tail = wait_for(
        tmux_exe, active_session, r"tmux active follow-up.*second line|second line", "active-run multiline follow-up draft"
    )
    transcript_rows = lambda screen: tuple(screen.splitlines()[:-4])
    active_live_rows = transcript_rows(active_live_tail)
    send_literal(tmux_exe, active_session, "\x1b[1;129A")
    active_arrow_scrolled = wait_for_screen_state(
        tmux_exe,
        active_session,
        lambda screen: transcript_rows(screen) != active_live_rows and "tmux active follow-up" in screen and "second line" in screen,
        "active-run physical Ghostty arrow transcript movement",
    )
    # /help intentionally lists the jump_to_bottom action name; only the
    # removed detached-banner phrases indicate stale product chrome.
    if any(text in active_arrow_scrolled for text in ("scrollback detached", "updates below")):
        raise RuntimeError(f"active-run arrow scroll surfaced deleted detached chrome\nscreen:\n{active_arrow_scrolled}")
    send_literal(tmux_exe, active_session, "X")
    active_multiline_cursor = wait_for(
        tmux_exe, active_session, r"second lineX", "active-run multiline cursor preserved by arrow scroll"
    )
    if "follow-upX" in active_multiline_cursor:
        raise RuntimeError(
            "active-run arrow moved the multiline composer cursor instead of scrolling only the transcript\n"
            f"screen:\n{active_multiline_cursor}"
        )
    send_literal(tmux_exe, active_session, "\x1b[1;129B")
    active_arrow_tail = wait_for_screen_state(
        tmux_exe,
        active_session,
        lambda screen: transcript_rows(screen) == active_live_rows and "tmux active follow-up" in screen and "second lineX" in screen,
        "active-run physical Ghostty arrow return to live tail",
    )
    active_wheel_live_rows = transcript_rows(active_arrow_tail)
    send_literal(tmux_exe, active_session, "\x1b[<64;4;6M")
    active_wheel_scrolled = wait_for_screen_state(
        tmux_exe,
        active_session,
        lambda screen: transcript_rows(screen) != active_wheel_live_rows and "tmux active follow-up" in screen and "second lineX" in screen,
        "active-run mouse wheel transcript movement",
    )
    if any(text in active_wheel_scrolled for text in ("scrollback detached", "updates below")):
        raise RuntimeError(f"active-run wheel scroll surfaced deleted detached chrome\nscreen:\n{active_wheel_scrolled}")
    # A harmless non-wheel ordering boundary at the end of the draft resets the
    # physical-wheel burst governor without changing the draft or cursor.
    send_literal(tmux_exe, active_session, "\x1b[1;129C\x1b[<65;4;6M")
    active_wheel_tail = wait_for_screen_state(
        tmux_exe,
        active_session,
        lambda screen: transcript_rows(screen) == active_wheel_live_rows and "tmux active follow-up" in screen and "second lineX" in screen,
        "active-run mouse wheel return to live tail",
    )
    send_literal(tmux_exe, active_session, "\x1b\r")
    queued_follow_up = wait_for(tmux_exe, active_session, r"follow-up queued", "active-run Alt+Enter follow-up queued")
    if "tmux active follow-up" not in queued_follow_up:
        raise RuntimeError(f"active-run Alt+Enter did not render the queued follow-up text\nscreen:\n{queued_follow_up}")
    save_evidence(root, "active-run-follow-up-queued", queued_follow_up)
    active_log = _wait_for_normal_turn_request_count(active_request_log, 2, "active-run queued follow-up provider request", timeout=12.0)
    if "tmux active first prompt" not in active_log or "tmux active follow-up" not in active_log or "second lineX" not in active_log:
        raise RuntimeError(f"active-run follow-up did not reach the fake provider intact\nrequest log:\n{active_log}")
    wait_for(tmux_exe, active_session, r"follow-up started|headless active prompt complete", "active-run follow-up delivery")
    send_keys(tmux_exe, active_session, "C-d")
    wait_for_session_exit(tmux_exe, active_session)
    tmux(tmux_exe, "kill-session", "-t", active_session, check=False)
