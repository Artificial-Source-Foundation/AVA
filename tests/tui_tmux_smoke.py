#!/usr/bin/env python3
"""Dispatch the independent opt-in tmux TUI smoke scenarios."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import shutil
import signal
import sys

from tui_smoke_helpers import (
    SKIP,
    SmokeContext,
    assert_request_count_stays,
    assert_screen_absent_for,
    assert_screen_present_for,
    capture,
    capture_styled,
    enabled,
    pane_cursor_position,
    save_evidence,
    selected_modal_identity,
    selected_modal_row,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_absent,
    wait_for_count,
    wait_for_cursor_change,
    wait_for_json_file,
    wait_for_pane_command,
    wait_for_request_count,
    wait_for_screen_change,
    wait_for_selected_modal_change,
    wait_for_session_exit,
)


SCENARIOS = (
    "suspend_resume",
    "keybind_conflict",
    "theme_env",
    "theme_persisted",
    "active_run",
    "restore_followup",
    "main_startup_trust_keybinds",
    "main_models_selectors",
    "main_editor_input",
    "main_slash_completions",
    "main_permission_flow",
    "main_session_mgmt",
    "main_paste_scrollback_attach",
)


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


def scenario_suspend_resume(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    workspace = ctx.workspace
    ava_config = ctx.ava_config
    suspend_session = ctx.session_name("suspend")
    suspend_env_prefix = ctx.pane_command(
        home=ctx.home,
        config=ctx.config,
        state=ctx.state,
        data=ctx.data,
        extra={"NO_COLOR": "1", "COLORFGBG": "", "VISUAL": "", "EDITOR": ctx.editor_command},
        exec_ava=False,
    )
    ava_config.joinpath("keybinds.json").unlink(missing_ok=True)
    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        suspend_session,
        "-x",
        "100",
        "-y",
        "24",
        "-c",
        str(workspace),
        "/bin/sh",
    )
    wait_for_pane_command(tmux_exe, suspend_session, r"(?:zsh|bash|sh|fish)$", "interactive shell before suspend smoke")
    send_literal(tmux_exe, suspend_session, suspend_env_prefix)
    send_keys(tmux_exe, suspend_session, "Enter")
    wait_for(tmux_exe, suspend_session, r"Type a message|live session", "suspend initial TUI frame")
    send_literal(tmux_exe, suspend_session, "suspend draft")
    wait_for(tmux_exe, suspend_session, r"suspend draft", "suspend draft before Ctrl+Z")
    send_keys(tmux_exe, suspend_session, "C-z")
    wait_for_pane_command(tmux_exe, suspend_session, r"(?:zsh|bash|sh|fish)$", "shell after Ctrl+Z suspend")
    send_literal(tmux_exe, suspend_session, "fg")
    send_keys(tmux_exe, suspend_session, "Enter")
    wait_for_pane_command(tmux_exe, suspend_session, r"ava$", "AVA foreground command after fg resume")
    resumed_suspend = wait_for(tmux_exe, suspend_session, r"suspend draft", "TUI redraw after fg resume")
    if "suspend draft" not in resumed_suspend:
        raise RuntimeError(f"suspend/resume did not preserve the draft\nscreen:\n{resumed_suspend}")
    send_keys(tmux_exe, suspend_session, "C-u")
    wait_for_absent(tmux_exe, suspend_session, r"suspend draft", "suspend draft cleared before exit")
    send_keys(tmux_exe, suspend_session, "C-d")
    wait_for_pane_command(tmux_exe, suspend_session, r"(?:zsh|bash|sh|fish)$", "interactive shell after resumed AVA exits")
    send_literal(tmux_exe, suspend_session, "exit")
    send_keys(tmux_exe, suspend_session, "Enter")
    wait_for_session_exit(tmux_exe, suspend_session)


def scenario_keybind_conflict(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    workspace = ctx.workspace
    ava_config = ctx.ava_config
    conflict_session = ctx.session_name("conflict")
    env_prefix = ctx.main_pane_command()
    (ava_config / "keybinds.json").write_text(
        '{"submit":"Ctrl+P","model_cycle_forward":"Ctrl+P"}\n', encoding="utf-8"
    )
    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        conflict_session,
        "-x",
        "120",
        "-y",
        "32",
        "-c",
        str(workspace),
        env_prefix,
    )
    conflict_screen = wait_for(
        tmux_exe,
        conflict_session,
        r"conflicting TUI keybinding|key: Ctrl\+P",
        "keybinding conflict startup diagnostic",
    )
    if "conflicting TUI keybinding" not in conflict_screen or "Ctrl+P" not in conflict_screen:
        raise RuntimeError(f"keybinding conflict diagnostic did not render visibly\nscreen:\n{conflict_screen}")
    tmux(tmux_exe, "kill-session", "-t", conflict_session, check=False)


def scenario_theme_env(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    workspace = ctx.workspace
    ava_config = ctx.ava_config
    theme_session = ctx.session_name("theme")
    background_theme_session = ctx.session_name("theme-bg")
    light_env_prefix = ctx.pane_command(
        home=ctx.home, config=ctx.config, state=ctx.state, data=ctx.data,
        extra={"NO_COLOR": "", "AVA_TUI_THEME": "light", "COLORFGBG": ""},
    )
    background_theme_env_prefix = ctx.pane_command(
        home=ctx.home, config=ctx.config, state=ctx.state, data=ctx.data,
        extra={"NO_COLOR": "", "AVA_TUI_THEME": "", "COLORFGBG": "0;15"},
    )
    (ava_config / "keybinds.json").write_text(
        '{"tui.editor.cursorLineStart":["Home","Ctrl+A"]}\n', encoding="utf-8"
    )
    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        theme_session,
        "-x",
        "100",
        "-y",
        "24",
        "-c",
        str(workspace),
        light_env_prefix,
    )
    wait_for(tmux_exe, theme_session, r"Type a message|live session", "light-theme initial TUI frame")
    send_literal(tmux_exe, theme_session, "/settings")
    wait_for(tmux_exe, theme_session, r"/settings", "light-theme settings command draft")
    send_keys(tmux_exe, theme_session, "Enter")
    light_settings_modal = wait_for(
        tmux_exe, theme_session, r"ava-light|AVA_TUI_THEME", "light-theme settings modal"
    )
    if "ava-light" not in light_settings_modal or "AVA_TUI_THEME" not in light_settings_modal:
        raise RuntimeError(f"settings modal did not report AVA_TUI_THEME=light\nscreen:\n{light_settings_modal}")
    tmux(tmux_exe, "kill-session", "-t", theme_session, check=False)

    display_config = ava_config / "display.json"
    if display_config.exists():
        display_config.unlink()
    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        background_theme_session,
        "-x",
        "100",
        "-y",
        "24",
        "-c",
        str(workspace),
        background_theme_env_prefix,
    )
    wait_for(
        tmux_exe,
        background_theme_session,
        r"Type a message|live session",
        "terminal-background theme initial TUI frame",
    )
    send_literal(tmux_exe, background_theme_session, "/settings")
    wait_for(tmux_exe, background_theme_session, r"/settings", "terminal-background settings command draft")
    send_keys(tmux_exe, background_theme_session, "Enter")
    background_theme_modal = wait_for(
        tmux_exe,
        background_theme_session,
        r"ava-light|COLORFGBG",
        "terminal-background settings modal",
    )
    if "ava-light" not in background_theme_modal or "COLORFGBG" not in background_theme_modal:
        raise RuntimeError(
            f"settings modal did not report COLORFGBG-derived light theme\nscreen:\n{background_theme_modal}"
        )
    tmux(tmux_exe, "kill-session", "-t", background_theme_session, check=False)


def scenario_theme_persisted(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    workspace = ctx.workspace
    ava_config = ctx.ava_config
    persisted_theme_session = ctx.session_name("theme-persist")
    display_config = ava_config / "display.json"
    persisted_theme_env_prefix = ctx.pane_command(
        home=ctx.home, config=ctx.config, state=ctx.state, data=ctx.data,
        extra={"NO_COLOR": "", "AVA_TUI_THEME": "", "COLORFGBG": ""},
    )
    (ava_config / "keybinds.json").write_text(
        '{"tui.editor.cursorLineStart":["Home","Ctrl+A"]}\n', encoding="utf-8"
    )
    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        persisted_theme_session,
        "-x",
        "100",
        "-y",
        "24",
        "-c",
        str(workspace),
        persisted_theme_env_prefix,
    )
    wait_for(tmux_exe, persisted_theme_session, r"Type a message|live session", "persisted-theme initial TUI frame")
    send_literal(tmux_exe, persisted_theme_session, "/settings")
    wait_for(tmux_exe, persisted_theme_session, r"/settings", "persisted-theme settings command draft")
    send_keys(tmux_exe, persisted_theme_session, "Enter")
    wait_for(tmux_exe, persisted_theme_session, r"Settings|Search settings", "persisted-theme settings modal")
    send_literal(tmux_exe, persisted_theme_session, "Theme light")
    wait_for(tmux_exe, persisted_theme_session, r"Theme light", "persisted-theme filtered theme row")
    send_keys(tmux_exe, persisted_theme_session, "Enter")
    applied_theme = wait_for(
        tmux_exe, persisted_theme_session, r"Stored TUI theme light", "settings theme selection applied"
    )
    if "Stored TUI theme light" not in applied_theme:
        raise RuntimeError(f"settings modal did not apply the light theme row\nscreen:\n{applied_theme}")
    if not display_config.exists() or '"theme": "light"' not in display_config.read_text(encoding="utf-8"):
        raise RuntimeError(f"settings theme selection did not write display.json\npath:\n{display_config}")
    tmux(tmux_exe, "kill-session", "-t", persisted_theme_session, check=False)

    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        persisted_theme_session,
        "-x",
        "100",
        "-y",
        "24",
        "-c",
        str(workspace),
        persisted_theme_env_prefix,
    )
    wait_for(tmux_exe, persisted_theme_session, r"Type a message|live session", "persisted-theme restart TUI frame")
    send_literal(tmux_exe, persisted_theme_session, "/settings")
    wait_for(tmux_exe, persisted_theme_session, r"/settings", "persisted-theme restart settings draft")
    send_keys(tmux_exe, persisted_theme_session, "Enter")
    persisted_theme_modal = wait_for(
        tmux_exe, persisted_theme_session, r"ava-light|display\.json", "persisted-theme settings modal after restart"
    )
    if "ava-light" not in persisted_theme_modal or "display.json" not in persisted_theme_modal:
        raise RuntimeError(f"settings modal did not report persisted display.json light theme\nscreen:\n{persisted_theme_modal}")
    send_keys(tmux_exe, persisted_theme_session, "Escape")
    wait_for_absent(tmux_exe, persisted_theme_session, r"Search settings", "persisted-theme settings modal canceled")
    send_keys(tmux_exe, persisted_theme_session, "C-u")
    send_literal(tmux_exe, persisted_theme_session, "/settings")
    wait_for(tmux_exe, persisted_theme_session, r"/settings", "custom-theme settings draft")
    send_keys(tmux_exe, persisted_theme_session, "Enter")
    wait_for(tmux_exe, persisted_theme_session, r"Settings|Search settings", "custom-theme settings modal")
    send_literal(tmux_exe, persisted_theme_session, "Theme ocean")
    wait_for(tmux_exe, persisted_theme_session, r"Theme ocean", "custom-theme filtered theme row")
    send_keys(tmux_exe, persisted_theme_session, "Enter")
    applied_custom_theme = wait_for(
        tmux_exe, persisted_theme_session, r"Stored TUI theme ocean", "settings custom theme selection applied"
    )
    if "Stored TUI theme ocean" not in applied_custom_theme:
        raise RuntimeError(f"settings modal did not apply the custom theme row\nscreen:\n{applied_custom_theme}")
    if '"theme": "ocean"' not in display_config.read_text(encoding="utf-8"):
        raise RuntimeError(f"settings custom theme selection did not write display.json\npath:\n{display_config}")
    display_config.write_text('{\n  "theme": "plain"\n}\n', encoding="utf-8")
    send_keys(tmux_exe, persisted_theme_session, "C-u")
    send_literal(tmux_exe, persisted_theme_session, "/reload theme")
    wait_for(tmux_exe, persisted_theme_session, r"/reload theme", "display theme reload draft")
    send_keys(tmux_exe, persisted_theme_session, "Enter")
    reloaded_theme = wait_for(
        tmux_exe, persisted_theme_session, r"display theme reloaded: plain", "display theme reload command"
    )
    if "display theme reloaded: plain" not in reloaded_theme:
        raise RuntimeError(f"/reload theme did not report the externally edited plain theme\nscreen:\n{reloaded_theme}")
    send_keys(tmux_exe, persisted_theme_session, "C-u")
    send_literal(tmux_exe, persisted_theme_session, "/settings")
    wait_for(tmux_exe, persisted_theme_session, r"/settings", "reloaded-theme settings draft")
    send_keys(tmux_exe, persisted_theme_session, "Enter")
    reloaded_theme_modal = wait_for(
        tmux_exe, persisted_theme_session, r"plain|display\.json", "settings modal after display theme reload"
    )
    if "plain" not in reloaded_theme_modal or "display.json" not in reloaded_theme_modal:
        raise RuntimeError(f"settings modal did not report reloaded display.json plain theme\nscreen:\n{reloaded_theme_modal}")
    send_keys(tmux_exe, persisted_theme_session, "Escape")
    wait_for_absent(tmux_exe, persisted_theme_session, r"Search settings", "reloaded-theme settings modal canceled")
    display_config.write_text('{\n  "theme": "light"\n}\n', encoding="utf-8")
    auto_reloaded_theme = wait_for(
        tmux_exe,
        persisted_theme_session,
        r"display theme auto-reloaded: ava-light",
        "automatic display theme reload",
    )
    if "display theme auto-reloaded: ava-light" not in auto_reloaded_theme:
        raise RuntimeError(f"display.json edit did not auto-reload the light theme\nscreen:\n{auto_reloaded_theme}")
    send_keys(tmux_exe, persisted_theme_session, "C-u")
    send_literal(tmux_exe, persisted_theme_session, "/settings")
    wait_for(tmux_exe, persisted_theme_session, r"/settings", "auto-reloaded-theme settings draft")
    send_keys(tmux_exe, persisted_theme_session, "Enter")
    auto_reloaded_theme_modal = wait_for(
        tmux_exe,
        persisted_theme_session,
        r"ava-light|display\.json",
        "settings modal after automatic display theme reload",
    )
    if "ava-light" not in auto_reloaded_theme_modal or "display.json" not in auto_reloaded_theme_modal:
        raise RuntimeError(
            f"settings modal did not report auto-reloaded display.json light theme\nscreen:\n{auto_reloaded_theme_modal}"
        )
    tmux(tmux_exe, "kill-session", "-t", persisted_theme_session, check=False)


def scenario_active_run(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    root = ctx.root
    active_workspace = ctx.active_workspace
    active_session = ctx.session_name("active")
    active_provider = ctx.start_fake_provider("active", delay_ms=8000)
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
    send_literal(tmux_exe, active_session, "tmux active first prompt")
    wait_for(tmux_exe, active_session, r"tmux active first prompt", "active-run first prompt draft")
    send_keys(tmux_exe, active_session, "Enter")
    wait_for_request_count(active_request_log, 1, "active-run first provider request")
    send_literal(tmux_exe, active_session, "/")
    wait_for(tmux_exe, active_session, r"/help|Show commands", "active-run slash palette")
    send_literal(tmux_exe, active_session, "\x1b[1;129B")
    wait_for(tmux_exe, active_session, r"› /hotkeys|> /hotkeys", "active-run physical Ghostty arrow palette navigation")
    send_keys(tmux_exe, active_session, "C-u")
    wait_for_absent(tmux_exe, active_session, r"› /hotkeys|> /hotkeys", "active-run slash palette cleared")
    send_literal(tmux_exe, active_session, "\x1b[200~tmux active follow-up\nsecond line\x1b[201~")
    wait_for(tmux_exe, active_session, r"tmux active follow-up.*second line|second line", "active-run multiline follow-up draft")
    send_literal(tmux_exe, active_session, "\x1b[1;129A")
    active_arrow_scrolled = wait_for(
        tmux_exe, active_session, r"scrollback detached", "active-run physical Ghostty arrow scrollback"
    )
    if "tmux active follow-up" not in active_arrow_scrolled or "second line" not in active_arrow_scrolled:
        raise RuntimeError(
            "active-run physical arrow changed the composer draft instead of scrolling only the transcript\n"
            f"screen:\n{active_arrow_scrolled}"
        )
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
    active_arrow_tail = wait_for_absent(
        tmux_exe, active_session, r"scrollback detached", "active-run physical Ghostty arrow return to live tail"
    )
    if "tmux active follow-up" not in active_arrow_tail or "second lineX" not in active_arrow_tail:
        raise RuntimeError(
            "active-run down arrow changed the composer draft while returning to the live tail\n"
            f"screen:\n{active_arrow_tail}"
        )
    send_literal(tmux_exe, active_session, "\x1b[<64;4;6M")
    active_wheel_scrolled = wait_for(
        tmux_exe, active_session, r"scrollback detached", "active-run mouse wheel scrollback"
    )
    if "tmux active follow-up" not in active_wheel_scrolled or "second lineX" not in active_wheel_scrolled:
        raise RuntimeError(
            "active-run mouse wheel changed the composer draft instead of scrolling only the transcript\n"
            f"screen:\n{active_wheel_scrolled}"
        )
    send_literal(tmux_exe, active_session, "\x1b[<65;4;6M")
    active_wheel_tail = wait_for_absent(
        tmux_exe, active_session, r"scrollback detached", "active-run mouse wheel return to live tail"
    )
    if "tmux active follow-up" not in active_wheel_tail or "second lineX" not in active_wheel_tail:
        raise RuntimeError(
            "active-run mouse wheel changed the composer draft while returning to the live tail\n"
            f"screen:\n{active_wheel_tail}"
        )
    send_literal(tmux_exe, active_session, "\x1b\r")
    queued_follow_up = wait_for(tmux_exe, active_session, r"follow-up queued", "active-run Alt+Enter follow-up queued")
    if "tmux active follow-up" not in queued_follow_up:
        raise RuntimeError(f"active-run Alt+Enter did not render the queued follow-up text\nscreen:\n{queued_follow_up}")
    save_evidence(root, "active-run-follow-up-queued", queued_follow_up)
    active_log = wait_for_request_count(active_request_log, 2, "active-run queued follow-up provider request", timeout=12.0)
    if "tmux active first prompt" not in active_log or "tmux active follow-up" not in active_log or "second lineX" not in active_log:
        raise RuntimeError(f"active-run follow-up did not reach the fake provider intact\nrequest log:\n{active_log}")
    wait_for(tmux_exe, active_session, r"follow-up started|headless active prompt complete", "active-run follow-up delivery")
    send_keys(tmux_exe, active_session, "C-d")
    wait_for_session_exit(tmux_exe, active_session)
    tmux(tmux_exe, "kill-session", "-t", active_session, check=False)


def scenario_restore_followup(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    root = ctx.root
    restore_workspace = ctx.restore_workspace
    restore_active_session = ctx.session_name("restore")
    restore_provider = ctx.start_fake_provider("restore", delay_ms=3500)
    restore_request_log = restore_provider.request_log
    restore_env_prefix = ctx.fake_provider_command(
        restore_provider,
        home=ctx.restore_home,
        config=ctx.restore_config,
        state=ctx.restore_state,
        data=ctx.restore_data,
    )
    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        restore_active_session,
        "-x",
        "110",
        "-y",
        "28",
        "-c",
        str(restore_workspace),
        restore_env_prefix,
    )
    wait_for(tmux_exe, restore_active_session, r"Type a message|live session", "active-run restore initial frame")
    send_literal(tmux_exe, restore_active_session, "tmux restore first prompt")
    wait_for(tmux_exe, restore_active_session, r"tmux restore first prompt", "active-run restore first prompt draft")
    send_keys(tmux_exe, restore_active_session, "Enter")
    wait_for_request_count(restore_request_log, 1, "active-run restore first provider request")
    send_literal(tmux_exe, restore_active_session, "tmux restore follow-up")
    wait_for(tmux_exe, restore_active_session, r"tmux restore follow-up", "active-run restore follow-up draft")
    send_literal(tmux_exe, restore_active_session, "\x1b\r")
    queued_restore_follow_up = wait_for(
        tmux_exe,
        restore_active_session,
        r"follow-up queued",
        "active-run restore Alt+Enter follow-up queued",
    )
    if "tmux restore follow-up" not in queued_restore_follow_up:
        raise RuntimeError(
            f"active-run restore setup did not render the queued follow-up text\nscreen:\n{queued_restore_follow_up}"
        )
    send_keys(tmux_exe, restore_active_session, "M-Up")
    restored_follow_up = wait_for(
        tmux_exe,
        restore_active_session,
        r"restored to composer|follow-up restored",
        "active-run Alt+Up follow-up restored",
    )
    if "tmux restore follow-up" not in restored_follow_up:
        raise RuntimeError(
            f"active-run Alt+Up did not restore the follow-up text visibly\nscreen:\n{restored_follow_up}"
        )
    save_evidence(root, "active-run-follow-up-restored", restored_follow_up)
    send_literal(tmux_exe, restore_active_session, " still-draft")
    restored_draft_edit = wait_for(
        tmux_exe,
        restore_active_session,
        r"tmux restore follow-up still-draft",
        "active-run restored follow-up remains editable",
    )
    if "tmux restore follow-up still-draft" not in restored_draft_edit:
        raise RuntimeError(
            f"active-run restored follow-up was not editable in the composer\nscreen:\n{restored_draft_edit}"
        )
    wait_for(
        tmux_exe,
        restore_active_session,
        r"headless active prompt complete",
        "active-run restore original prompt completion",
        timeout=12.0,
    )
    restore_log = assert_request_count_stays(
        restore_request_log,
        1,
        "active-run restored follow-up should not be delivered",
    )
    if "tmux restore follow-up" in restore_log:
        raise RuntimeError(f"restored follow-up leaked to the fake provider\nrequest log:\n{restore_log}")
    send_keys(tmux_exe, restore_active_session, "C-u")
    send_keys(tmux_exe, restore_active_session, "C-d")
    wait_for_session_exit(tmux_exe, restore_active_session)
    tmux(tmux_exe, "kill-session", "-t", restore_active_session, check=False)


def scenario_main_startup_trust_keybinds(ctx: SmokeContext) -> None:
    # Preserve the original precedence assertion without depending on the
    # theme-persistence scenario: NO_COLOR must override a stored light theme.
    ctx.ava_config.joinpath("display.json").write_text('{"theme":"light"}\n', encoding="utf-8")
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)
    import_keybinds_content = ctx.import_keybinds_content
    initial = wait_for(tmux_exe, session, r"Type a message|live session", "initial TUI frame")
    if "AVA" not in initial:
        raise RuntimeError(f"initial frame did not show AVA branding\nscreen:\n{initial}")
    if "Provider auth is not configured for `openai`" not in initial or "Connect with /connect" not in initial:
        raise RuntimeError(f"first-run onboarding guidance did not render in fresh TUI\nscreen:\n{initial}")
    footer_lines = [line for line in initial.splitlines() if "GPT-5.5" in line and "ctx " in line]
    if not footer_lines or any(
        marker in footer_lines[-1]
        for marker in ("Build", "OpenAI", "cwd ", "git ", "entries ", "%")
    ):
        raise RuntimeError(
            "composer footer did not contain only the model name and context count\n"
            f"screen:\n{initial}"
        )
    save_evidence(root, "startup-ready-composer", initial)
    styled_initial = capture_styled(tmux_exe, session)
    if "\x1b[" in styled_initial:
        raise RuntimeError(f"NO_COLOR=1 TUI frame still captured ANSI style escapes\nscreen:\n{styled_initial}")

    def wait_for_idle_composer_reflow(height: int, label: str) -> tuple[str, list[str]]:
        input_row = height - 2
        footer_row = height - 1
        settled = wait_for(
            tmux_exe,
            session,
            rf"(?m)\A(?:[^\n]*\n){{{input_row}}}│  Type a message\.\.\.[^\n]*\n│  GPT-5\.5 · ctx \d+[^\n]*(?:\n|\Z)",
            f"{label} target composer/footer rows {input_row}/{footer_row}",
        )
        settled_lines = settled.splitlines()
        if len(settled_lines) <= footer_row:
            raise RuntimeError(
                f"{label} did not contain target composer/footer rows {input_row}/{footer_row}\nscreen:\n{settled}"
            )
        if not settled_lines[input_row].startswith("│  Type a message..."):
            raise RuntimeError(
                f"{label} input row did not start with the quiet composer prefix at row {input_row}\nscreen:\n{settled}"
            )
        if not settled_lines[footer_row].startswith("│  "):
            raise RuntimeError(
                f"{label} footer did not start with the quiet composer prefix at row {footer_row}\nscreen:\n{settled}"
            )
        if "❯" in settled:
            raise RuntimeError(f"{label} retained the removed composer prompt glyph\nscreen:\n{settled}")
        return settled, settled_lines

    def capture_idle_shell(width: int, height: int, name: str, sidebar_expected: bool) -> None:
        previous = capture(tmux_exe, session)
        tmux(tmux_exe, "resize-window", "-t", session, "-x", str(width), "-y", str(height))
        wait_for_screen_change(
            tmux_exe, session, previous, f"{name} resize redraw"
        )
        dimensions = tmux(
            tmux_exe, "display-message", "-p", "-t", session, "#{window_width},#{window_height}"
        ).stdout.strip()
        if dimensions != f"{width},{height}":
            raise RuntimeError(f"{name} dimensions were {dimensions}, expected {width},{height}")
        settled, settled_lines = wait_for_idle_composer_reflow(height, name)
        if re.search(r"traceback|assert(?:ion)?|failure", settled, re.IGNORECASE):
            raise RuntimeError(f"{name} idle frame shows failure text\nscreen:\n{settled}")
        settled_footer = settled_lines[height - 1][3:]
        if sidebar_expected:
            settled_footer = settled_footer.split("│", 1)[0]
        settled_footer = settled_footer.strip()
        if not re.fullmatch(r"GPT-5\.5 · ctx \d+", settled_footer):
            raise RuntimeError(
                f"{name} footer did not contain only the idle model name and context count\nscreen:\n{settled}"
            )
        sidebar_pattern = r"live session|Activity"
        if sidebar_expected and ("live session" not in settled or "Activity" not in settled):
            raise RuntimeError(f"{name} did not show the current sidebar\nscreen:\n{settled}")
        if not sidebar_expected and re.search(sidebar_pattern, settled):
            raise RuntimeError(f"{name} unexpectedly showed the sidebar\nscreen:\n{settled}")
        unexpected_controls = [
            character for character in settled if ord(character) < 32 and character != "\n"
        ]
        if unexpected_controls:
            raise RuntimeError(f"{name} saved frame contains unexpected C0 controls\nscreen:\n{settled}")
        save_evidence(root, name, settled)

    capture_idle_shell(160, 48, "frontend-f1-wide-idle-composer", sidebar_expected=True)
    capture_idle_shell(120, 36, "frontend-f1-ordinary-idle-composer", sidebar_expected=True)
    capture_idle_shell(80, 24, "frontend-f1-narrow-idle-composer", sidebar_expected=False)
    capture_idle_shell(100, 12, "frontend-f1-short-idle-composer", sidebar_expected=False)

    restore_previous = capture(tmux_exe, session)
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "120", "-y", "32")
    wait_for_screen_change(tmux_exe, session, restore_previous, "startup baseline restore redraw")
    restored_dimensions = tmux(
        tmux_exe, "display-message", "-p", "-t", session, "#{window_width},#{window_height}"
    ).stdout.strip()
    if restored_dimensions != "120,32":
        raise RuntimeError(f"startup baseline restore dimensions were {restored_dimensions}, expected 120,32")
    wait_for_idle_composer_reflow(32, "startup baseline restored")

    send_literal(tmux_exe, session, "/copy")
    wait_for(tmux_exe, session, r"/copy", "empty copy command draft")
    send_keys(tmux_exe, session, "Enter")
    empty_copy = wait_for(tmux_exe, session, r"no AVA messages to copy", "empty /copy status")
    if "no AVA messages to copy" not in empty_copy:
        raise RuntimeError(f"/copy without prior AVA messages did not report the empty copy state\nscreen:\n{empty_copy}")
    send_keys(tmux_exe, session, "C-u")

    send_literal(tmux_exe, session, "/settings")
    wait_for(tmux_exe, session, r"/settings", "settings command draft")
    send_keys(tmux_exe, session, "Enter")
    settings_modal = wait_for(tmux_exe, session, r"Settings|Search settings", "settings modal")
    if "plain" not in settings_modal or "NO_COLOR" not in settings_modal:
        raise RuntimeError(f"settings modal did not report the active NO_COLOR plain mode\nscreen:\n{settings_modal}")
    save_evidence(root, "settings-plain-no-color", settings_modal)
    styled_settings = capture_styled(tmux_exe, session)
    if "\x1b[" in styled_settings:
        raise RuntimeError(f"NO_COLOR=1 settings modal still captured ANSI style escapes\nscreen:\n{styled_settings}")
    send_literal(tmux_exe, session, "trust")
    settings_trust_rows = wait_for(
        tmux_exe, session, r"Search: trust", "settings trust filtered rows"
    )
    if (
        "Project trust" not in settings_trust_rows
        or "project resources" not in settings_trust_rows
        or "Trust status" not in settings_trust_rows
        or "Trust project" not in settings_trust_rows
        or "Deny project" not in settings_trust_rows
    ):
        raise RuntimeError(
            f"settings modal did not expose project trust status and actions\nscreen:\n{settings_trust_rows}"
        )
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Settings|Search:", "settings modal closed after trust rows")

    send_literal(tmux_exe, session, "/settings")
    wait_for(tmux_exe, session, r"/settings", "settings command draft before trust status action")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Settings|Search settings", "settings modal before trust status action")
    send_literal(tmux_exe, session, "trust status")
    settings_trust_status_row = wait_for(
        tmux_exe, session, r"Search: trust status", "settings trust status filtered row"
    )
    if "Trust status" not in settings_trust_status_row or "/trust status" not in settings_trust_status_row:
        raise RuntimeError(
            f"settings modal did not expose the trust status action when filtered\nscreen:\n{settings_trust_status_row}"
        )
    send_keys(tmux_exe, session, "Enter")
    settings_trust_status = wait_for(
        tmux_exe,
        session,
        r"(?s)Project trust:.*decision=unknown.*project_resources=skipped.*protected_resources=3",
        "settings trust status action output",
    )
    if "prompt_commands" not in settings_trust_status or "system_prompt" not in settings_trust_status:
        raise RuntimeError(
            f"settings trust status action did not render protected-resource diagnostics\nscreen:\n{settings_trust_status}"
        )

    send_literal(tmux_exe, session, "/settings")
    wait_for(tmux_exe, session, r"/settings", "settings command draft before keybinding rows")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Settings|Search settings", "settings modal before keybinding rows")
    send_literal(tmux_exe, session, "Keybindings")
    wait_for(tmux_exe, session, r"Search: Keybindings", "settings keybinding filter state")
    settings_keybinding_rows = wait_for(tmux_exe, session, r"Keybindings file", "settings keybinding filtered rows")
    keybindings_row = next(
        (
            (index + 1, line)
            for index, line in enumerate(settings_keybinding_rows.splitlines())
            if "Keybindings" in line
            and "open" in line
            and "file" not in line
            and "edit" not in line
            and "reload" not in line
        ),
        None,
    )
    if keybindings_row is None:
        raise RuntimeError(
            f"settings keybinding rows did not expose a clickable open row\nscreen:\n{settings_keybinding_rows}"
        )
    keybindings_row_number, keybindings_row_text = keybindings_row
    keybindings_column = max(1, len(keybindings_row_text) - len(keybindings_row_text.lstrip()) + 4)
    send_literal(tmux_exe, session, f"\x1b[<0;{keybindings_column};{keybindings_row_number}M")
    settings_opened_hotkeys = wait_for(
        tmux_exe, session, r"Search keybindings|mode_toggle", "settings mouse click opens keybindings view"
    )
    if "Search keybindings" not in settings_opened_hotkeys:
        raise RuntimeError(
            f"settings keybindings row mouse click did not open the active keybindings view\nscreen:\n{settings_opened_hotkeys}"
        )
    send_literal(tmux_exe, session, "cursor_left")
    wait_for(tmux_exe, session, r"cursor_left", "settings-opened keybindings filtered action")
    send_keys(tmux_exe, session, "Enter")
    hotkeys_edit_draft = wait_for(
        tmux_exe, session, r"/keybindings set cursor_left", "settings-opened keybindings drafts selected action"
    )
    if "/keybindings set cursor_left" not in hotkeys_edit_draft:
        raise RuntimeError(
            f"settings-opened keybindings view did not draft the selected action edit command\nscreen:\n{hotkeys_edit_draft}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/settings")
    wait_for(tmux_exe, session, r"/settings", "settings command draft before keybinding validate")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Settings|Search settings", "settings modal before keybinding validate")
    send_literal(tmux_exe, session, "validate")
    wait_for(tmux_exe, session, r"Keybindings file", "settings keybinding validate row")
    send_keys(tmux_exe, session, "Enter")
    settings_validate = wait_for(tmux_exe, session, r"keybindings file is valid", "settings keybinding validate action")
    if "keybindings file is valid" not in settings_validate:
        raise RuntimeError(f"settings keybinding validate row did not run validation\nscreen:\n{settings_validate}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/settings")
    wait_for(tmux_exe, session, r"/settings", "settings command draft before keybinding edit")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Settings|Search settings", "settings modal before keybinding edit")
    send_literal(tmux_exe, session, "keybindings edit")
    wait_for(tmux_exe, session, r"Keybindings edit", "settings keybinding edit row")
    send_keys(tmux_exe, session, "Enter")
    settings_edit_draft = wait_for(tmux_exe, session, r"/keybindings set", "settings keybinding edit drafts command")
    if "/keybindings set" not in settings_edit_draft:
        raise RuntimeError(f"settings keybinding edit row did not draft /keybindings set\nscreen:\n{settings_edit_draft}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/settings")
    wait_for(tmux_exe, session, r"/settings", "settings command draft before keybinding reload")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Settings|Search settings", "settings modal before keybinding reload")
    send_literal(tmux_exe, session, "reload")
    settings_reload_row = wait_for(
        tmux_exe, session, r"Search: reload[^\n]*\n(?:[^\n]*\n)*[^\n]*Keybindings reload", "settings keybinding reload row"
    )
    if "Keybindings reload" not in settings_reload_row or "/reload keybindings" not in settings_reload_row:
        raise RuntimeError(
            f"settings modal did not expose keybinding reload guidance when filtered\nscreen:\n{settings_reload_row}"
        )
    send_keys(tmux_exe, session, "Enter")
    settings_reload = wait_for(tmux_exe, session, r"keybindings reloaded", "settings keybinding reload action")
    if "keybindings reloaded" not in settings_reload:
        raise RuntimeError(f"settings keybinding reload row did not reload live bindings\nscreen:\n{settings_reload}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/settings")
    wait_for(tmux_exe, session, r"/settings", "settings command draft before model selector")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Settings|Search settings", "settings modal before model selector")
    send_literal(tmux_exe, session, "Model selector")
    settings_model_selector_row = wait_for(
        tmux_exe, session, r"Model selector|/models selector", "settings model selector row"
    )
    if "Model selector" not in settings_model_selector_row or "openai/GPT" not in settings_model_selector_row:
        raise RuntimeError(
            f"settings modal did not expose the model selector action\nscreen:\n{settings_model_selector_row}"
        )
    send_keys(tmux_exe, session, "Enter")
    settings_opened_model_selector = wait_for(
        tmux_exe, session, r"Select model|Search models", "settings opens model selector"
    )
    if "Select model" not in settings_opened_model_selector and "Search models" not in settings_opened_model_selector:
        raise RuntimeError(
            f"settings model selector row did not open the model selector\nscreen:\n{settings_opened_model_selector}"
        )
    save_evidence(root, "settings-model-selector", settings_opened_model_selector)
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Select model|Search models", "settings-opened model selector canceled")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/settings")
    wait_for(tmux_exe, session, r"/settings", "settings command draft before scoped models")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Settings|Search settings", "settings modal before scoped models")
    send_literal(tmux_exe, session, "cycle scope")
    settings_scoped_model_row = wait_for(
        tmux_exe, session, r"Model cycle scope|/scoped-models|Ctrl\+S", "settings scoped model row"
    )
    if (
        "Model cycle scope" not in settings_scoped_model_row
        or "Ctrl+P scoped cycle" not in settings_scoped_model_row
    ):
        raise RuntimeError(
            f"settings modal did not expose scoped model-cycle persistence guidance\nscreen:\n{settings_scoped_model_row}"
        )
    send_keys(tmux_exe, session, "Enter")
    settings_opened_scoped_model_selector = wait_for(
        tmux_exe, session, r"Scoped model cycle|Search models", "settings opens scoped model selector"
    )
    if "Scoped model cycle" not in settings_opened_scoped_model_selector:
        raise RuntimeError(
            f"settings scoped model row did not open the scoped cycle selector\nscreen:\n{settings_opened_scoped_model_selector}"
        )
    save_evidence(root, "settings-scoped-model-selector", settings_opened_scoped_model_selector)
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(
        tmux_exe, session, r"Scoped model cycle|Search models", "settings-opened scoped model selector canceled"
    )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/context")
    wait_for(tmux_exe, session, r"/context\s+Sessions", "context command palette")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/context\s+Sessions", "context palette dismissed")
    send_keys(tmux_exe, session, "Enter")
    context_freshness = wait_for(
        tmux_exe, session, r"(?s)Context freshness:.*context_sources=1", "context freshness command"
    )
    context_freshness_section = context_freshness.rsplit("Context freshness:", 1)[-1]
    if (
        "prompt=builtin" not in context_freshness_section
        or "context_sources=1" not in context_freshness_section
        or "loaded_bytes=19" not in context_freshness_section
        or "status=current" not in context_freshness_section
        or "project_resources=skipped" not in context_freshness_section
        or "system_prompt_sources=0" not in context_freshness_section
    ):
        raise RuntimeError(f"/context did not report prompt and context freshness visibly\nscreen:\n{context_freshness}")
    if "trust-smoke" in context_freshness_section:
        raise RuntimeError(f"/context listed an untrusted project prompt command\nscreen:\n{context_freshness}")
    if "APPEND_SYSTEM" in context_freshness_section:
        raise RuntimeError(f"/context listed an untrusted project append-system prompt\nscreen:\n{context_freshness}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/trust status")
    wait_for(tmux_exe, session, r"/trust status", "trust status draft")
    send_keys(tmux_exe, session, "Enter")
    trust_status = wait_for(
        tmux_exe, session, r"(?s)Project trust:.*decision=unknown.*project_resources=skipped", "trust status command"
    )
    if (
        "protected_resources=3" not in trust_status
        or "prompt_commands" not in trust_status
        or "plugins" not in trust_status
        or "system_prompt" not in trust_status
    ):
        raise RuntimeError(f"/trust status did not list protected project resources\nscreen:\n{trust_status}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/trust project")
    wait_for(tmux_exe, session, r"/trust project", "trust project draft")
    send_keys(tmux_exe, session, "Enter")
    trust_project = wait_for(
        tmux_exe, session, r"(?s)trusted project resources.*project_resources=enabled", "trust project command"
    )
    if "decision=trusted" not in trust_project:
        raise RuntimeError(f"/trust project did not persist a trusted decision\nscreen:\n{trust_project}")
    trust_project = wait_for(
        tmux_exe,
        session,
        r"GPT-5\.5\s+·\s+ctx 2",
        "composer footer context count after project trust reload",
    )
    save_evidence(root, "footer-context-count-refreshed", trust_project)

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/context trust-smoke ")
    wait_for(tmux_exe, session, r"│  /context trust-smoke", "trusted context query draft")
    send_keys(tmux_exe, session, "Enter")
    trusted_context = wait_for(
        tmux_exe,
        session,
        r"(?s)project_trust=trusted project_resources=enabled.*prompt_command\s+project\s+trust-smoke",
        "trusted project prompt command freshness",
    )
    if "status=current" not in trusted_context:
        raise RuntimeError(f"/context did not report trusted project prompt command freshness\nscreen:\n{trusted_context}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/context APPEND_SYSTEM ")
    wait_for(tmux_exe, session, r"│  /context APPEND_SYSTEM", "trusted append-system context query draft")
    send_keys(tmux_exe, session, "Enter")
    trusted_prompt_context = wait_for(
        tmux_exe,
        session,
        r"(?s)project_trust=trusted project_resources=enabled.*append_system_prompt\s+project\s+APPEND_SYSTEM\.md",
        "trusted project append-system freshness",
    )
    if "status=current" not in trusted_prompt_context:
        raise RuntimeError(f"/context did not report trusted append-system prompt freshness\nscreen:\n{trusted_prompt_context}")

    (ava_config / "keybinds.json").unlink(missing_ok=True)
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/keybindings init")
    wait_for(tmux_exe, session, r"│  /keybindings init(?:\s|$)", "keybindings init command draft")
    wait_for(tmux_exe, session, r"Create \$XDG_CONFIG_HOME/ava/keybinds\.json", "keybindings init completion row")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Create \$XDG_CONFIG_HOME/ava/keybinds\.json", "keybindings init palette dismissed")
    send_keys(tmux_exe, session, "Enter")
    keybindings_init = wait_for(
        tmux_exe, session, r"Created keybindings starter file", "keybindings starter init command"
    )
    if "Created keybindings starter file" not in keybindings_init:
        raise RuntimeError(f"/keybindings init did not report starter-file creation\nscreen:\n{keybindings_init}")
    keybinds_content = (ava_config / "keybinds.json").read_text(encoding="utf-8")
    if '"tui.input.submit"' not in keybinds_content or '"tui.editor.cursorLeft"' not in keybinds_content:
        raise RuntimeError(f"/keybindings init wrote an unexpected starter file\ncontent:\n{keybinds_content}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/keybindings init")
    wait_for(tmux_exe, session, r"│  /keybindings init(?:\s|$)", "keybindings existing init command draft")
    wait_for(tmux_exe, session, r"Create \$XDG_CONFIG_HOME/ava/keybinds\.json", "keybindings existing init completion row")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Create \$XDG_CONFIG_HOME/ava/keybinds\.json", "keybindings existing init palette dismissed")
    send_keys(tmux_exe, session, "Enter")
    keybindings_init_existing = wait_for(
        tmux_exe, session, r"keybindings file already exists", "keybindings starter overwrite refusal"
    )
    if "--force" not in keybindings_init_existing:
        raise RuntimeError(
            f"/keybindings init did not explain the explicit overwrite path\nscreen:\n{keybindings_init_existing}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/keybindings init --force")
    wait_for(tmux_exe, session, r"/keybindings init --force", "keybindings force init draft")
    send_keys(tmux_exe, session, "Enter")
    keybindings_init_force = wait_for(
        tmux_exe, session, r"Replaced keybindings starter file", "keybindings starter force command"
    )
    if "Replaced keybindings starter file" not in keybindings_init_force:
        raise RuntimeError(f"/keybindings init --force did not replace the starter file\nscreen:\n{keybindings_init_force}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/keybindings validate")
    wait_for(tmux_exe, session, r"/keybindings validate|Validate \$XDG_CONFIG_HOME/ava/keybinds", "keybindings validate palette row")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Validate \$XDG_CONFIG_HOME/ava/keybinds", "keybindings validate palette dismissed")
    send_keys(tmux_exe, session, "Enter", "Enter")
    keybindings_validate = wait_for(
        tmux_exe, session, r"keybindings file is valid", "keybindings validate command"
    )
    if "keybindings file is valid" not in keybindings_validate:
        raise RuntimeError(f"/keybindings validate did not report a valid starter file\nscreen:\n{keybindings_validate}")

    (ava_config / "keybinds.json").write_text(
        '{"submit":"Ctrl+P","model_cycle_forward":"Ctrl+P"}\n', encoding="utf-8"
    )
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/keybindings validate")
    wait_for(tmux_exe, session, r"/keybindings validate|Validate \$XDG_CONFIG_HOME/ava/keybinds", "invalid keybindings validate palette row")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Validate \$XDG_CONFIG_HOME/ava/keybinds", "invalid keybindings validate palette dismissed")
    send_keys(tmux_exe, session, "Enter", "Enter")
    invalid_keybindings_validate = wait_for(
        tmux_exe, session, r"keybindings file is invalid|conflicting TUI keybinding|Ctrl\+P", "invalid keybindings validate command"
    )
    if "keybindings file is invalid" not in invalid_keybindings_validate or "Ctrl+P" not in invalid_keybindings_validate:
        raise RuntimeError(
            f"/keybindings validate did not report the conflicting keybinding\nscreen:\n{invalid_keybindings_validate}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/keybindings import import-keybinds.json --force")
    wait_for(tmux_exe, session, r"/keybindings import import-keybinds\.json --force", "keybindings import draft")
    send_keys(tmux_exe, session, "Enter")
    keybindings_import = wait_for(tmux_exe, session, r"Imported keybindings file", "keybindings import command")
    if "Imported keybindings file" not in keybindings_import or "/reload keybindings" not in keybindings_import:
        raise RuntimeError(f"/keybindings import did not report an installed file\nscreen:\n{keybindings_import}")
    imported_keybinds = (ava_config / "keybinds.json").read_text(encoding="utf-8")
    if imported_keybinds != import_keybinds_content:
        raise RuntimeError(
            f"/keybindings import did not install the expected keybindings file\ncontent:\n{imported_keybinds}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/keybindings set cursor_left Alt+H")
    wait_for(tmux_exe, session, r"/keybindings set cursor_left Alt\+H", "keybindings set draft")
    send_keys(tmux_exe, session, "Enter")
    keybindings_set = wait_for(tmux_exe, session, r"Set keybinding", "keybindings set command")
    if "Set keybinding" not in keybindings_set or "/reload keybindings" not in keybindings_set:
        raise RuntimeError(f"/keybindings set did not report an edited keybinding\nscreen:\n{keybindings_set}")
    edited_keybinds = (ava_config / "keybinds.json").read_text(encoding="utf-8")
    if '"tui.editor.cursorLeft": "Alt+H"' not in edited_keybinds or '"cursor_left"' in edited_keybinds:
        raise RuntimeError(
            f"/keybindings set did not edit the keybindings file as expected\ncontent:\n{edited_keybinds}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/keybindings reset cursor_left")
    wait_for(tmux_exe, session, r"/keybindings reset cursor_left", "keybindings reset draft")
    send_keys(tmux_exe, session, "Enter")
    keybindings_reset = wait_for(tmux_exe, session, r"Reset keybinding override", "keybindings reset command")
    if "Reset keybinding override" not in keybindings_reset or "/reload keybindings" not in keybindings_reset:
        raise RuntimeError(f"/keybindings reset did not report a reset override\nscreen:\n{keybindings_reset}")
    reset_keybinds = (ava_config / "keybinds.json").read_text(encoding="utf-8")
    if "tui.editor.cursorLeft" in reset_keybinds or "cursor_left" in reset_keybinds:
        raise RuntimeError(
            f"/keybindings reset did not remove the cursor-left override\ncontent:\n{reset_keybinds}"
        )

    send_literal(tmux_exe, session, "\x1b[Z")
    shift_tab_reasoning = wait_for(tmux_exe, session, r"reasoning set to low|reasoning low", "shift-tab reasoning cycle")
    if "reasoning set to low" not in shift_tab_reasoning and "reasoning low" not in shift_tab_reasoning:
        raise RuntimeError(f"Shift+Tab did not cycle the visible reasoning state\nscreen:\n{shift_tab_reasoning}")
    send_keys(tmux_exe, session, "C-t")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/thinking")
    wait_for(tmux_exe, session, r"/thinking", "ctrl-t thinking hide oracle draft")
    send_keys(tmux_exe, session, "Enter")
    thinking_hidden_key = wait_for(
        tmux_exe, session, r"thinking blocks are now visible", "ctrl-t thinking hide oracle"
    )
    if "thinking blocks are now visible" not in thinking_hidden_key:
        raise RuntimeError(f"Ctrl+T did not hide thinking blocks before /thinking restored them\nscreen:\n{thinking_hidden_key}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/thinking")
    wait_for(tmux_exe, session, r"/thinking", "thinking command hide draft")
    send_keys(tmux_exe, session, "Enter")
    thinking_hidden_command = wait_for(
        tmux_exe, session, r"thinking blocks are now hidden", "thinking command hides blocks"
    )
    if "thinking blocks are now hidden" not in thinking_hidden_command:
        raise RuntimeError(f"/thinking did not hide thinking blocks before Ctrl+T show check\nscreen:\n{thinking_hidden_command}")
    send_keys(tmux_exe, session, "C-t")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/thinking")
    wait_for(tmux_exe, session, r"/thinking", "ctrl-t thinking show oracle draft")
    send_keys(tmux_exe, session, "Enter")
    thinking_visible_key = wait_for_count(
        tmux_exe, session, r"thinking blocks are now hidden", 2, "ctrl-t thinking show oracle"
    )
    if len(re.findall(r"thinking blocks are now hidden", thinking_visible_key)) < 2:
        raise RuntimeError(f"Ctrl+T did not show thinking blocks before /thinking hid them again\nscreen:\n{thinking_visible_key}")

    _finish_main(tmux_exe, session)


def scenario_main_models_selectors(ctx: SmokeContext) -> None:
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)
    scoped_persist_session = ctx.session_name("scoped-persist")
    send_keys(tmux_exe, session, "C-p")
    model_cycle = wait_for(
        tmux_exe,
        session,
        r"model cycled|GPT-5\.6 Sol|gpt-5\.6-sol|GPT-4\.1 mini|gpt-4\.1-mini",
        "ctrl-p model cycle",
    )
    if not any(value in model_cycle for value in ("model cycled", "GPT-5.6 Sol", "gpt-5.6-sol", "GPT-4.1 mini", "gpt-4.1-mini")):
        raise RuntimeError(f"Ctrl+P did not cycle the visible model state\nscreen:\n{model_cycle}")
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "82", "-y", "10")
    wait_for(tmux_exe, session, r"Type a message|live session", "compact frame before selector navigation")
    send_literal(tmux_exe, session, "/models")
    wait_for(tmux_exe, session, r"/models", "exact models selector draft")
    send_keys(tmux_exe, session, "Enter")
    command_model_selector = wait_for(tmux_exe, session, r"Select model|Search models", "exact models selector")
    if "Select model" not in command_model_selector and "Search models" not in command_model_selector:
        raise RuntimeError(f"Exact /models did not bypass autocomplete and open the model selector\nscreen:\n{command_model_selector}")
    selected_row = selected_modal_row(command_model_selector)
    if not selected_row:
        raise RuntimeError(f"Model selector did not expose a selected row before navigation\nscreen:\n{command_model_selector}")
    selected_rows = {selected_modal_identity(selected_row)}
    initial_model_selector = command_model_selector
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "82", "-y", "11")
    resized_model_selector = wait_for(tmux_exe, session, r"Select model|Search models", "resized compact model selector")
    resized_selected_row = selected_modal_row(resized_model_selector)
    if not resized_selected_row or selected_modal_identity(resized_selected_row) != selected_modal_identity(selected_row):
        raise RuntimeError(
            "Model selector lost its selected row while resizing between compact terminal heights\n"
            f"before:\n{command_model_selector}\nafter:\n{resized_model_selector}"
        )
    selected_row = resized_selected_row
    send_keys(tmux_exe, session, "Down")
    selected_row, _ = wait_for_selected_modal_change(
        tmux_exe, session, selected_row, "tmux Down arrow model navigation"
    )
    selected_rows.add(selected_modal_identity(selected_row))
    send_literal(tmux_exe, session, "\x1b[1;129B")
    selected_row, _ = wait_for_selected_modal_change(
        tmux_exe, session, selected_row, "physical Ghostty CSI arrow model navigation with Num Lock"
    )
    selected_rows.add(selected_modal_identity(selected_row))
    for step in range(7):
        send_keys(tmux_exe, session, "Down")
        selected_row, _ = wait_for_selected_modal_change(
            tmux_exe, session, selected_row, f"model selector navigation step {step + 3}"
        )
        selected_rows.add(selected_modal_identity(selected_row))
    if len(selected_rows) < 9:
        raise RuntimeError(
            "Arrow navigation did not visit nine distinct model rows\n"
            f"visited: {sorted(selected_rows)}\nscreen:\n{capture(tmux_exe, session)}"
        )
    if not any(identity and identity not in initial_model_selector for identity in selected_rows):
        raise RuntimeError(
            "Model selector selection never advanced beyond the initial compact viewport\n"
            f"visited: {sorted(selected_rows)}\ninitial screen:\n{initial_model_selector}\n"
            f"final screen:\n{capture(tmux_exe, session)}"
        )
    save_evidence(root, "model-selector-arrow-scroll", capture(tmux_exe, session))
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Select model|Search models", "exact models selector canceled")
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "82", "-y", "10")
    wait_for(tmux_exe, session, r"Type a message|live session", "compact frame before provider modal navigation")
    send_literal(tmux_exe, session, "/connect")
    wait_for(tmux_exe, session, r"/connect", "provider modal command draft")
    send_keys(tmux_exe, session, "Enter")
    provider_modal = wait_for(tmux_exe, session, r"Connect a provider|Select provider", "provider question modal")
    provider_selected_row = selected_modal_row(provider_modal)
    if not provider_selected_row:
        raise RuntimeError(f"Provider question modal did not expose a selected row\nscreen:\n{provider_modal}")
    send_literal(tmux_exe, session, "\x1b[1;129B")
    _, provider_modal_after_arrow = wait_for_selected_modal_change(
        tmux_exe, session, provider_selected_row, "provider modal physical Ghostty arrow navigation with Num Lock"
    )
    save_evidence(root, "provider-modal-arrow-navigation", provider_modal_after_arrow)
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Connect a provider|Select provider", "provider question modal canceled")
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "120", "-y", "32")
    wait_for(tmux_exe, session, r"Type a message|live session", "restored frame after compact modal navigation")
    send_keys(tmux_exe, session, "C-l")
    model_selector = wait_for(tmux_exe, session, r"Select model|Search models", "ctrl-l model selector")
    if "Select model" not in model_selector and "Search models" not in model_selector:
        raise RuntimeError(f"Ctrl+L did not open the model selector\nscreen:\n{model_selector}")
    send_literal(tmux_exe, session, "Diagnostic")
    diagnostic_model_selector = wait_for(
        tmux_exe, session, r"Diagnostic Local.*diagnostics|diagnostics.*Diagnostic Local", "model selector diagnostics"
    )
    if "Diagnostic Local" not in diagnostic_model_selector or "diagnostics" not in diagnostic_model_selector:
        raise RuntimeError(
            f"Model selector did not show the custom model diagnostics\nscreen:\n{diagnostic_model_selector}"
        )
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Select model|Search models", "model selector canceled")
    send_literal(tmux_exe, session, "/scoped-models")
    wait_for(tmux_exe, session, r"/scoped-models", "scoped model selector draft")
    send_keys(tmux_exe, session, "Enter")
    scoped_model_selector = wait_for(
        tmux_exe, session, r"Scoped model cycle|Search models", "scoped model selector"
    )
    if "Scoped model cycle" not in scoped_model_selector or "Ctrl+X clear" not in scoped_model_selector:
        raise RuntimeError(
            f"/scoped-models did not open the scoped cycle selector with model controls\nscreen:\n{scoped_model_selector}"
        )
    send_literal(tmux_exe, session, "Diagnostic")
    wait_for(tmux_exe, session, r"Diagnostic Local", "scoped model reorder filtered row")
    send_keys(tmux_exe, session, "M-Up")
    send_keys(tmux_exe, session, *("BSpace" for _ in "Diagnostic"))
    wait_for(tmux_exe, session, r"Search: Search models", "scoped model reorder filter clear acknowledgement")
    send_keys(tmux_exe, session, "C-s")
    saved_reordered_models = wait_for_json_file(
        ava_config / "models.json",
        lambda value: isinstance(value, dict)
        and isinstance(value.get("scoped_model_cycle"), list)
        and len(value["scoped_model_cycle"]) >= 2
        and "openai/diagnostic-local" in value["scoped_model_cycle"],
        "persisted reordered scoped model cycle",
    )
    saved_reordered_cycle = json.loads(saved_reordered_models).get("scoped_model_cycle")
    if (
        not isinstance(saved_reordered_cycle, list)
        or len(saved_reordered_cycle) < 2
        or "openai/diagnostic-local" not in saved_reordered_cycle
    ):
        raise RuntimeError(
            "Alt+Up did not make the scoped model cycle explicit before saving\n"
            f"content:\n{saved_reordered_models}"
        )
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Scoped model cycle|Search models", "scoped model reorder selector canceled")
    send_literal(tmux_exe, session, "/scoped-models")
    wait_for(tmux_exe, session, r"/scoped-models", "scoped model selector draft after reorder")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Scoped model cycle|Search models", "scoped model selector after reorder")
    send_keys(tmux_exe, session, "C-x")
    scoped_model_cleared = wait_for(
        tmux_exe, session, r"0/[0-9]+ enabled|disabled", "scoped model selector clear visible"
    )
    if "0/" not in scoped_model_cleared or "disabled" not in scoped_model_cleared:
        raise RuntimeError(
            f"Ctrl+X did not clear the visible scoped model cycle\nscreen:\n{scoped_model_cleared}"
        )
    send_keys(tmux_exe, session, "C-s")
    saved_empty_models = wait_for_json_file(
        ava_config / "models.json",
        lambda value: isinstance(value, dict)
        and value.get("scoped_model_cycle") == []
        and any(model.get("name") == "Diagnostic Local" for model in value.get("models", []) if isinstance(model, dict)),
        "persisted empty scoped model cycle",
    )
    if '"scoped_model_cycle": []' not in saved_empty_models or "Diagnostic Local" not in saved_empty_models:
        raise RuntimeError(
            f"Ctrl+S did not persist the empty scoped model cycle while preserving custom models\ncontent:\n{saved_empty_models}"
        )
    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        scoped_persist_session,
        "-x",
        "100",
        "-y",
        "24",
        "-c",
        str(workspace),
        env_prefix,
    )
    wait_for(tmux_exe, scoped_persist_session, r"Type a message|live session", "scoped model restart frame")
    send_keys(tmux_exe, scoped_persist_session, "C-p")
    scoped_model_cycle_restart_empty = wait_for(
        tmux_exe,
        scoped_persist_session,
        r"enabled for cycling|no registered provider models",
        "persisted empty scoped model cycle status",
    )
    if (
        "enabled for cycling" not in scoped_model_cycle_restart_empty
        and "no registered provider models" not in scoped_model_cycle_restart_empty
    ):
        raise RuntimeError(
            "A fresh TUI did not load the persisted empty scoped model cycle\n"
            f"screen:\n{scoped_model_cycle_restart_empty}"
        )
    send_keys(tmux_exe, scoped_persist_session, "C-d")
    wait_for_session_exit(tmux_exe, scoped_persist_session)
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Scoped model cycle|Search models", "scoped model selector canceled")
    send_keys(tmux_exe, session, "C-p")
    scoped_model_cycle_empty = wait_for(
        tmux_exe, session, r"enabled for cycling|no registered provider models", "empty scoped model cycle status"
    )
    if "enabled for cycling" not in scoped_model_cycle_empty and "no registered provider models" not in scoped_model_cycle_empty:
        raise RuntimeError(
            f"Ctrl+P did not report the empty scoped model cycle\nscreen:\n{scoped_model_cycle_empty}"
        )
    send_literal(tmux_exe, session, "/scoped-models")
    wait_for(tmux_exe, session, r"/scoped-models", "scoped model selector restore draft")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Scoped model cycle|Search models", "scoped model selector restore")
    send_keys(tmux_exe, session, "C-a")
    scoped_model_enabled = wait_for(
        tmux_exe, session, r"all configured registered models enabled|all-enabled", "scoped model selector enable visible"
    )
    if "all configured registered models enabled" not in scoped_model_enabled and "all-enabled" not in scoped_model_enabled:
        raise RuntimeError(
            f"Ctrl+A did not restore the visible scoped model cycle\nscreen:\n{scoped_model_enabled}"
    )
    send_keys(tmux_exe, session, "C-s")
    saved_all_models = wait_for_json_file(
        ava_config / "models.json",
        lambda value: isinstance(value, dict)
        and "scoped_model_cycle" not in value
        and any(model.get("name") == "Diagnostic Local" for model in value.get("models", []) if isinstance(model, dict)),
        "persisted all-model scoped cycle",
    )
    if "scoped_model_cycle" in saved_all_models or "Diagnostic Local" not in saved_all_models:
        raise RuntimeError(
            f"Ctrl+S did not remove the scoped model cycle field while preserving custom models\ncontent:\n{saved_all_models}"
        )
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Scoped model cycle|Search models", "scoped model selector restore canceled")
    send_keys(tmux_exe, session, "C-p")
    restored_model_cycle = wait_for(
        tmux_exe,
        session,
        r"model cycled|GPT-5\.6 (?:Sol|Terra|Luna)|gpt-5\.6-(?:sol|terra|luna)|GPT-4\.1 mini|gpt-4\.1-mini|Claude Sonnet 4\.5|claude-sonnet-4-5",
        "restored scoped model cycle",
    )
    restored_cycle_markers = (
        "model cycled",
        "GPT-5.6 Sol",
        "GPT-5.6 Terra",
        "GPT-5.6 Luna",
        "gpt-5.6-sol",
        "gpt-5.6-terra",
        "gpt-5.6-luna",
        "GPT-4.1 mini",
        "gpt-4.1-mini",
        "Claude Sonnet 4.5",
        "claude-sonnet-4-5",
    )
    if not any(value in restored_model_cycle for value in restored_cycle_markers):
        raise RuntimeError(f"Ctrl+P did not cycle after restoring scoped models\nscreen:\n{restored_model_cycle}")

    _finish_main(tmux_exe, session)


def scenario_main_editor_input(ctx: SmokeContext) -> None:
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)
    (ava_config / "keybinds.json").write_text(
        '{"tui.editor.cursorLineStart":["Home","Ctrl+A","Alt+Up","Insert"],'
        '"tui.editor.cursorLineEnd":["F2","Ctrl+1"],'
        '"tui.editor.cursorLeft":["Left","Ctrl+B","Alt+H"],'
        '"tui.editor.cursorWordRight":["Ctrl+Right","Alt+Right","Alt+F","Alt+W"],'
        '"tui.editor.deleteCharBackward":["Shift+Backspace","Ctrl+H"],'
        '"tui.editor.deleteCharForward":["Shift+Delete","Delete"],'
        '"app.session.resume":"Alt+J",'
        '"app.session.new":"Alt+K",'
        '"tui.select.confirm":["Enter","Space"],'
        '"tui.select.cancel":["Escape","Ctrl+W"]}\n',
        encoding="utf-8",
    )
    send_literal(tmux_exe, session, "/reload")
    wait_for(tmux_exe, session, r"/reload \[all\|theme\|models", "reload palette row")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/reload \[all\|theme\|models", "reload palette dismissed")
    send_keys(tmux_exe, session, "Enter")
    reload_screen = wait_for(tmux_exe, session, r"keybindings reloaded", "live keybinding reload")
    if "keybindings reloaded" not in reload_screen:
        raise RuntimeError(f"/reload did not report a live keybinding reload\nscreen:\n{reload_screen}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "fkey")
    send_keys(tmux_exe, session, "C-a")
    send_keys(tmux_exe, session, "F2")
    send_literal(tmux_exe, session, "Z")
    fkey_end = wait_for(tmux_exe, session, r"fkeyZ", "custom F2 cursor-end binding")
    if "fkeyZ" not in fkey_end or "Zfkey" in fkey_end:
        raise RuntimeError(f"F2 custom binding did not move the composer cursor to the end\nscreen:\n{fkey_end}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "insert")
    send_keys(tmux_exe, session, "Insert")
    send_literal(tmux_exe, session, "Z")
    insert_start = wait_for(tmux_exe, session, r"Zinsert", "custom Insert cursor-start binding")
    if "Zinsert" not in insert_start or "insertZ" in insert_start:
        raise RuntimeError(f"Insert custom binding did not move the composer cursor to the start\nscreen:\n{insert_start}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "ctrlone")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1b[27;5;49~")
    send_literal(tmux_exe, session, "Z")
    ctrl_one_end = wait_for(tmux_exe, session, r"ctrloneZ", "custom Ctrl+1 cursor-end binding")
    if "ctrloneZ" not in ctrl_one_end or "Zctrlone" in ctrl_one_end:
        raise RuntimeError(f"Ctrl+1 custom binding did not move the composer cursor to the end\nscreen:\n{ctrl_one_end}")
    send_keys(tmux_exe, session, "C-u")
    send_keys(tmux_exe, session, "M-j")
    session_resume_key = wait_for(tmux_exe, session, r"Select session|Session tree", "custom session resume key")
    if "Select session" not in session_resume_key and "Session tree" not in session_resume_key:
        raise RuntimeError(f"Alt+J custom session resume key did not open selector\nscreen:\n{session_resume_key}")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Select session|Session tree", "custom session resume selector dismissed")
    send_keys(tmux_exe, session, "M-k")
    session_new_key = wait_for(tmux_exe, session, r"started session session_", "custom session new key")
    if "started session session_" not in session_new_key:
        raise RuntimeError(f"Alt+K custom session new key did not create a session\nscreen:\n{session_new_key}")
    send_literal(tmux_exe, session, "alt-up-visible")
    send_keys(tmux_exe, session, "M-Up")
    send_literal(tmux_exe, session, "Z")
    alt_up_delivery = wait_for(
        tmux_exe,
        session,
        r"Zalt-up-visible",
        "alt-up key delivery",
    )
    if "Zalt-up-visible" not in alt_up_delivery:
        raise RuntimeError(f"Alt+Up did not reach the TUI keybinding layer\nscreen:\n{alt_up_delivery}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "ctrlh")
    send_keys(tmux_exe, session, "C-h")
    send_literal(tmux_exe, session, "Z")
    ctrl_h_delete = wait_for(tmux_exe, session, r"ctrlZ", "ctrl-h delete backward binding")
    if "ctrlZ" not in ctrl_h_delete or "ctrlhZ" in ctrl_h_delete:
        raise RuntimeError(f"Ctrl+H did not delete the previous composer character\nscreen:\n{ctrl_h_delete}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "alth")
    send_keys(tmux_exe, session, "M-h")
    send_literal(tmux_exe, session, "Z")
    alt_h_left = wait_for(tmux_exe, session, r"altZh", "alt-h cursor-left binding")
    if "altZh" not in alt_h_left or "althZ" in alt_h_left:
        raise RuntimeError(f"Alt+H did not move the composer cursor left\nscreen:\n{alt_h_left}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "alpha beta")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1bw")
    send_literal(tmux_exe, session, "Y")
    alt_w_word = wait_for(tmux_exe, session, r"alphaY beta", "alt-w cursor-word-right binding")
    if "alphaY beta" not in alt_w_word or "Yalpha beta" in alt_w_word:
        raise RuntimeError(f"Alt+W did not move the composer cursor right by word\nscreen:\n{alt_w_word}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "alpha beta")
    click_cursor_draft = wait_for(tmux_exe, session, r"alpha beta", "composer mouse cursor draft")
    click_cursor_row = next(
        ((index + 1, line) for index, line in enumerate(click_cursor_draft.splitlines()) if "alpha beta" in line),
        None,
    )
    if click_cursor_row is None:
        raise RuntimeError(f"composer draft did not expose a clickable row\nscreen:\n{click_cursor_draft}")
    click_cursor_row_number, click_cursor_row_text = click_cursor_row
    click_cursor_column = click_cursor_row_text.index("alpha beta") + len("alpha ") + 1
    send_literal(tmux_exe, session, f"\x1b[<0;{click_cursor_column};{click_cursor_row_number}M")
    send_literal(tmux_exe, session, "Z")
    clicked_cursor = wait_for(tmux_exe, session, r"alpha Zbeta", "raw SGR composer cursor click")
    if "alpha Zbeta" not in clicked_cursor or "alpha betaZ" in clicked_cursor:
        raise RuntimeError(f"raw SGR composer click did not move the draft cursor\nscreen:\n{clicked_cursor}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "drag one")
    drag_cursor_draft = wait_for(tmux_exe, session, r"drag one", "composer mouse selection draft")
    drag_cursor_row = next(
        ((index + 1, line) for index, line in enumerate(drag_cursor_draft.splitlines()) if "drag one" in line),
        None,
    )
    if drag_cursor_row is None:
        raise RuntimeError(f"composer draft did not expose a draggable row\nscreen:\n{drag_cursor_draft}")
    drag_cursor_row_number, drag_cursor_row_text = drag_cursor_row
    drag_anchor_column = drag_cursor_row_text.index("drag one") + len("drag ") + 1
    drag_focus_column = drag_cursor_row_text.index("drag one") + len("drag one") + 1
    send_literal(tmux_exe, session, f"\x1b[<0;{drag_anchor_column};{drag_cursor_row_number}M")
    send_literal(tmux_exe, session, f"\x1b[<32;{drag_focus_column};{drag_cursor_row_number}M")
    send_literal(tmux_exe, session, f"\x1b[<0;{drag_focus_column};{drag_cursor_row_number}m")
    send_literal(tmux_exe, session, "TWO")
    dragged_selection = wait_for(tmux_exe, session, r"drag TWO", "raw SGR composer drag selection replacement")
    if "drag TWO" not in dragged_selection or "drag oneTWO" in dragged_selection:
        raise RuntimeError(f"raw SGR drag/release did not select and replace draft text\nscreen:\n{dragged_selection}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "copy me")
    wait_for(tmux_exe, session, r"copy me", "keyboard copy selection draft")
    cursor_before_selection = pane_cursor_position(tmux_exe, session)
    send_literal(tmux_exe, session, "\x1b[1;2D")
    cursor_after_first_selection = wait_for_cursor_change(
        tmux_exe, session, cursor_before_selection, "first Shift+Left selection"
    )
    send_literal(tmux_exe, session, "\x1b[1;2D")
    wait_for_cursor_change(tmux_exe, session, cursor_after_first_selection, "second Shift+Left selection")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "X")
    keyboard_selection = wait_for(tmux_exe, session, r"copy X", "keyboard selection replacement")
    if "copy X" not in keyboard_selection or "copy meX" in keyboard_selection:
        raise RuntimeError(f"Shift+Arrow selection did not stay replaceable after copy\nscreen:\n{keyboard_selection}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "before external")
    send_keys(tmux_exe, session, "C-g")
    external_editor = wait_for(
        tmux_exe,
        session,
        r"external editor draft|external editor updated draft",
        "Ctrl+G external editor draft replacement",
    )
    if "external editor draft" not in external_editor:
        raise RuntimeError(f"Ctrl+G external editor did not replace the visible draft\nscreen:\n{external_editor}")
    wait_for_pane_command(tmux_exe, session, r"^ava$", "external editor process return")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"external editor draft", "composer clear after external editor")
    send_literal(tmux_exe, session, "erase XY")
    wait_for(tmux_exe, session, r"erase XY", "Backspace selection draft")
    send_literal(tmux_exe, session, "\x1b[1;2D")
    send_literal(tmux_exe, session, "\x1b[1;2D")
    send_keys(tmux_exe, session, "C-h")
    backspace_deleted_selection = wait_for_absent(
        tmux_exe, session, r"erase XY", "Backspace selected composer text deletion"
    )
    if "erase" not in backspace_deleted_selection or "erase XY" in backspace_deleted_selection:
        raise RuntimeError(
            f"Backspace did not delete the selected composer text\nscreen:\n{backspace_deleted_selection}"
        )
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"erase", "composer clear before Delete selection test")
    send_literal(tmux_exe, session, "trim UV")
    wait_for(tmux_exe, session, r"trim UV", "Delete selection draft")
    send_literal(tmux_exe, session, "\x1b[1;2D")
    send_literal(tmux_exe, session, "\x1b[1;2D")
    send_keys(tmux_exe, session, "Delete")
    delete_removed_selection = wait_for_absent(
        tmux_exe, session, r"trim UV", "Delete selected composer text deletion"
    )
    if "trim" not in delete_removed_selection or "trim UV" in delete_removed_selection:
        raise RuntimeError(f"Delete did not delete the selected composer text\nscreen:\n{delete_removed_selection}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "shiftback")
    wait_for(tmux_exe, session, r"shiftback", "Shift+Backspace draft")
    send_literal(tmux_exe, session, "\x1b[127;2u")
    send_literal(tmux_exe, session, "Z")
    shift_backspace = wait_for(tmux_exe, session, r"shiftbacZ", "Shift+Backspace delete-backward alias")
    if "shiftbacZ" not in shift_backspace or "shiftbackZ" in shift_backspace:
        raise RuntimeError(f"Shift+Backspace did not delete the previous composer character\nscreen:\n{shift_backspace}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "shiftdelete")
    wait_for(tmux_exe, session, r"shiftdelete", "Shift+Delete draft")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1b[3$")
    send_literal(tmux_exe, session, "Z")
    shift_delete = wait_for(tmux_exe, session, r"Zhiftdelete", "Shift+Delete delete-forward alias")
    if "Zhiftdelete" not in shift_delete or "Zshiftdelete" in shift_delete:
        raise RuntimeError(f"Shift+Delete did not delete the next composer character\nscreen:\n{shift_delete}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "one two three")
    wait_for(tmux_exe, session, r"one two three", "word selection draft")
    send_literal(tmux_exe, session, "\x1b[1;6D")
    send_literal(tmux_exe, session, "THREE")
    word_selection = wait_for(tmux_exe, session, r"one two THREE", "Shift+Ctrl+Left word selection replacement")
    if "one two THREE" not in word_selection or "one two threeTHREE" in word_selection:
        raise RuntimeError(f"Shift+Ctrl+Left did not select and replace the previous word\nscreen:\n{word_selection}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "line start")
    wait_for(tmux_exe, session, r"line start", "line-start selection draft")
    send_literal(tmux_exe, session, "\x1b[1;2H")
    send_literal(tmux_exe, session, "home")
    line_start_selection = wait_for(tmux_exe, session, r"home", "Shift+Home line-start selection replacement")
    if "home" not in line_start_selection or "line starthome" in line_start_selection:
        raise RuntimeError(f"Shift+Home did not select and replace to the line start\nscreen:\n{line_start_selection}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "end line")
    wait_for(tmux_exe, session, r"end line", "line-end selection draft")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1b[1;2F")
    send_literal(tmux_exe, session, "END")
    line_end_selection = wait_for(tmux_exe, session, r"END", "Shift+End line-end selection replacement")
    if "END" not in line_end_selection or "ENDend line" in line_end_selection or "end lineEND" in line_end_selection:
        raise RuntimeError(f"Shift+End did not select and replace to the line end\nscreen:\n{line_end_selection}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "\x1b[200~sxhome alpha\nsxhome beta\x1b[201~")
    wait_for(tmux_exe, session, r"sxhome beta", "document-start selection draft")
    send_literal(tmux_exe, session, "\x1b[1;6H")
    send_literal(tmux_exe, session, "DOCSTART")
    document_start_selection = wait_for(
        tmux_exe, session, r"DOCSTART", "Shift+Ctrl+Home document-start selection replacement"
    )
    if "DOCSTART" not in document_start_selection or "sxhome" in document_start_selection:
        raise RuntimeError(
            "Shift+Ctrl+Home did not select and replace to the document start\n"
            f"screen:\n{document_start_selection}"
        )
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "\x1b[200~sxend alpha\nsxend beta\x1b[201~")
    wait_for(tmux_exe, session, r"sxend beta", "document-end selection draft")
    send_literal(tmux_exe, session, "\x1b[1;5H")
    send_literal(tmux_exe, session, "\x1b[1;6F")
    send_literal(tmux_exe, session, "DOCEND")
    document_end_selection = wait_for(
        tmux_exe, session, r"DOCEND", "Shift+Ctrl+End document-end selection replacement"
    )
    if "DOCEND" not in document_end_selection or "sxend" in document_end_selection:
        raise RuntimeError(
            "Ctrl+Home plus Shift+Ctrl+End did not select and replace to the document end\n"
            f"screen:\n{document_end_selection}"
        )
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "\x1b[200~top\nbot\x1b[201~")
    wait_for(tmux_exe, session, r"bot", "Shift+Up selection draft")
    send_literal(tmux_exe, session, "\x1b[1;2A")
    send_literal(tmux_exe, session, "UP")
    shift_up_selection = wait_for(tmux_exe, session, r"topUP", "Shift+Up vertical selection replacement")
    if "topUP" not in shift_up_selection or "botUP" in shift_up_selection:
        raise RuntimeError(f"Shift+Up did not select and replace the previous line span\nscreen:\n{shift_up_selection}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"topUP", "Shift+Up selection draft clear")
    send_literal(tmux_exe, session, "\x1b[200~one\ntwo\x1b[201~")
    send_literal(tmux_exe, session, "\x1b[1;5H")
    send_literal(tmux_exe, session, "\x1b[1;2B")
    send_literal(tmux_exe, session, "DOWN")
    shift_down_selection = wait_for(tmux_exe, session, r"DOWNtwo", "Shift+Down vertical selection replacement")
    if "DOWNtwo" not in shift_down_selection or "oneDOWN" in shift_down_selection:
        raise RuntimeError(f"Shift+Down did not select and replace the next line span\nscreen:\n{shift_down_selection}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "kitty ")
    send_literal(tmux_exe, session, "\x1b[57400u")
    send_literal(tmux_exe, session, " nav")
    kitty_keypad_text = wait_for(tmux_exe, session, r"kitty 1 nav", "Kitty CSI-u keypad printable input")
    if "kitty 1 nav" not in kitty_keypad_text:
        raise RuntimeError(f"Kitty CSI-u keypad printable input did not insert text\nscreen:\n{kitty_keypad_text}")
    send_literal(tmux_exe, session, "\x1b[57417u")
    send_literal(tmux_exe, session, "Z")
    kitty_keypad_left = wait_for(tmux_exe, session, r"kitty 1 naZv", "Kitty CSI-u keypad left navigation")
    if "kitty 1 naZv" not in kitty_keypad_left or "kitty 1 navZ" in kitty_keypad_left:
        raise RuntimeError(f"Kitty CSI-u keypad left did not move the composer cursor\nscreen:\n{kitty_keypad_left}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "neg ")
    send_literal(tmux_exe, session, "\x1b[?0u")
    send_literal(tmux_exe, session, "\x1b[?62;4;52c")
    send_literal(tmux_exe, session, "ok")
    negotiation_text = wait_for(tmux_exe, session, r"neg ok", "keyboard protocol negotiation replies ignored")
    if "neg ok" not in negotiation_text or "?0u" in negotiation_text or "?62;4;52c" in negotiation_text:
        raise RuntimeError(f"keyboard protocol negotiation replies leaked into the draft\nscreen:\n{negotiation_text}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "modify ")
    send_literal(tmux_exe, session, "\x1b[27;1;120~")
    send_literal(tmux_exe, session, "\x1b[27;2;69~")
    send_literal(tmux_exe, session, " key")
    modify_text = wait_for(tmux_exe, session, r"modify xE key", "xterm modifyOtherKeys printable input")
    if "modify xE key" not in modify_text:
        raise RuntimeError(f"xterm modifyOtherKeys printable input did not insert text\nscreen:\n{modify_text}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "mod one")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1b[27;3;100~")
    send_literal(tmux_exe, session, "Z")
    modify_alt_d = wait_for(tmux_exe, session, r"Z one", "xterm modifyOtherKeys Alt+D delete-forward")
    if "Z one" not in modify_alt_d or "Zmod one" in modify_alt_d:
        raise RuntimeError(f"xterm modifyOtherKeys Alt+D did not delete the next word\nscreen:\n{modify_alt_d}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "alpha beta gamma")
    send_keys(tmux_exe, session, "C-w")
    send_keys(tmux_exe, session, "C-w")
    send_keys(tmux_exe, session, "C-y")
    yanked_text = wait_for(tmux_exe, session, r"alpha beta", "ctrl-y kill-ring yank")
    if "alpha beta" not in yanked_text:
        raise RuntimeError(f"Ctrl+Y did not yank the latest kill-ring entry\nscreen:\n{yanked_text}")
    send_literal(tmux_exe, session, "\x1by")
    yank_pop = wait_for(tmux_exe, session, r"alpha gamma", "alt-y kill-ring yank-pop")
    if "alpha gamma" not in yank_pop:
        raise RuntimeError(f"Alt+Y did not cycle the yanked kill-ring entry\nscreen:\n{yank_pop}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "ctrl-enter-one")
    send_literal(tmux_exe, session, "\x1b[13;5u")
    send_literal(tmux_exe, session, "tail")
    modified_enter = wait_for(
        tmux_exe,
        session,
        r"ctrl-enter-one[^\n]*\n[^\n]*tail",
        "Ctrl+Enter newline alias",
    )
    if "ctrl-enter-onetail" in modified_enter:
        raise RuntimeError(f"Ctrl+Enter did not create a multiline draft break\nscreen:\n{modified_enter}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"ctrl-enter-one|tail", "Ctrl+Enter draft clear")

    send_literal(tmux_exe, session, "slash-newline\\")
    send_keys(tmux_exe, session, "Enter")
    send_literal(tmux_exe, session, "tail")
    backslash_enter = wait_for(
        tmux_exe,
        session,
        r"slash-newline[^\n]*\n[^\n]*tail",
        "backslash Enter newline workaround",
    )
    if "slash-newlinetail" in backslash_enter:
        raise RuntimeError(f"Backslash+Enter did not create a multiline draft break\nscreen:\n{backslash_enter}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"slash-newline|tail", "backslash Enter draft clear")

    send_literal(tmux_exe, session, "/help")
    wait_for(tmux_exe, session, r"/help", "Alt+Enter idle submit draft")
    send_literal(tmux_exe, session, "\x1b\r")
    alt_enter_help = wait_for(
        tmux_exe,
        session,
        r"page_up PageUp|model_cycle_forward|details_toggle|tree_fold_or_up|tree_unfold_or_down",
        "Alt+Enter idle submit help output",
    )
    if (
        "page_up PageUp" not in alt_enter_help
        and "model_cycle_forward" not in alt_enter_help
        and "details_toggle" not in alt_enter_help
        and "tree_fold_or_up" not in alt_enter_help
        and "tree_unfold_or_down" not in alt_enter_help
    ):
        raise RuntimeError(f"Alt+Enter did not submit the /help command while idle\nscreen:\n{alt_enter_help}")
    send_keys(tmux_exe, session, "C-u")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/keybindings")
    wait_for(tmux_exe, session, r"/keybindings", "keybindings alias draft")
    send_keys(tmux_exe, session, "Enter")
    keybindings_modal = wait_for(tmux_exe, session, r"Keybindings|keybindings opened", "keybindings alias modal")
    if "Keybindings" not in keybindings_modal:
        raise RuntimeError(f"/keybindings did not open the keybinding discovery modal\nscreen:\n{keybindings_modal}")
    send_keys(tmux_exe, session, "C-w")
    wait_for_absent(tmux_exe, session, r"Search keybindings", "keybindings modal canceled by custom Ctrl+W")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/keybindings")
    wait_for(tmux_exe, session, r"/keybindings", "keybindings alias draft for Space confirm")
    send_keys(tmux_exe, session, "Enter")
    keybindings_modal = wait_for(tmux_exe, session, r"Keybindings|keybindings opened", "keybindings alias modal for Space confirm")
    if "Keybindings" not in keybindings_modal:
        raise RuntimeError(f"/keybindings did not reopen the keybinding discovery modal\nscreen:\n{keybindings_modal}")
    send_keys(tmux_exe, session, "Space")
    wait_for_absent(tmux_exe, session, r"Search keybindings", "keybindings modal selected by custom Space")

    (ava_config / "keybinds.json").write_text(
        '{"tui.editor.cursorLineStart":["Home","Ctrl+A","Alt+Up"],'
        '"tui.editor.cursorLeft":["Left","Ctrl+B","Alt+H"],'
        '"tui.editor.cursorWordRight":["Ctrl+Right","Alt+Right","Alt+F","Alt+W"],'
        '"tui.editor.deleteCharBackward":["Ctrl+H"]}\n',
        encoding="utf-8",
    )
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/reload")
    wait_for(tmux_exe, session, r"/reload \[all\|theme\|models", "restore default select bindings reload row")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/reload \[all\|theme\|models", "restore default select bindings reload dismissed")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"keybindings reloaded", "default select bindings restored")

    _finish_main(tmux_exe, session)


def scenario_main_slash_completions(ctx: SmokeContext) -> None:
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)
    send_literal(tmux_exe, session, "/")
    palette = wait_for(tmux_exe, session, r"/help|Show commands", "slash palette")
    if "[200~" in palette or "[201~" in palette:
        raise RuntimeError(f"paste markers leaked before paste smoke\nscreen:\n{palette}")
    help_row = next(
        (index + 1 for index, line in enumerate(palette.splitlines()) if "/help" in line or "Show commands" in line),
        None,
    )
    if help_row is None:
        raise RuntimeError(f"slash palette did not expose a clickable help row\nscreen:\n{palette}")
    send_literal(tmux_exe, session, f"\x1b[<0;4;{help_row}M")
    clicked_help = wait_for(tmux_exe, session, r"│  /help(?:\s|$)", "raw SGR slash palette mouse click")
    if "/help" not in clicked_help:
        raise RuntimeError(f"raw SGR mouse click did not select the slash palette row\nscreen:\n{clicked_help}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/per")
    permissions_palette = wait_for(
        tmux_exe, session, r"/permissions|permission rules", "permission rule command palette"
    )
    if "/permissions" not in permissions_palette and "permission rules" not in permissions_palette:
        raise RuntimeError(f"permission command did not appear in the slash palette\nscreen:\n{permissions_palette}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/permissions list")
    send_keys(tmux_exe, session, "Enter")
    permissions_list = wait_for(tmux_exe, session, r"Permission rules:|No persistent permission rules", "permission rules command output")
    if "Permission rules:" not in permissions_list and "No persistent permission rules" not in permissions_list:
        raise RuntimeError(f"permission rules command did not render command output\nscreen:\n{permissions_list}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/rd")
    fuzzy_slash = wait_for(tmux_exe, session, r"/read", "fuzzy slash command palette")
    if "/read" not in fuzzy_slash:
        raise RuntimeError(f"fuzzy slash command palette did not show /read\nscreen:\n{fuzzy_slash}")
    send_keys(tmux_exe, session, "Tab")
    selected_fuzzy_slash = wait_for(tmux_exe, session, r"/read ", "fuzzy slash command selection")
    if "/read " not in selected_fuzzy_slash:
        raise RuntimeError(f"fuzzy slash command selection did not update the draft\nscreen:\n{selected_fuzzy_slash}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/models 4sonnet")
    fuzzy_model_arg = wait_for(
        tmux_exe, session, r"anthropic/claude-sonnet-4-5|Claude Sonnet", "fuzzy model argument completion"
    )
    if "claude-sonnet-4-5" not in fuzzy_model_arg and "Claude Sonnet" not in fuzzy_model_arg:
        raise RuntimeError(
            f"fuzzy model argument completion did not show the Sonnet model\nscreen:\n{fuzzy_model_arg}"
        )
    send_keys(tmux_exe, session, "Tab")
    selected_fuzzy_model_arg = wait_for(
        tmux_exe,
        session,
        r"/models (?:anthropic/)?claude-sonnet-4-5",
        "fuzzy model argument completion selection",
    )
    if "/models claude-sonnet-4-5" not in selected_fuzzy_model_arg and "/models anthropic/claude-sonnet-4-5" not in selected_fuzzy_model_arg:
        raise RuntimeError(
            "fuzzy model argument completion did not update the draft\n"
            f"screen:\n{selected_fuzzy_model_arg}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/models open")
    wait_for(tmux_exe, session, r"openai/gpt-5\.5|GPT-5\.5", "slash argument completion before cursor movement")
    send_keys(tmux_exe, session, "Left", "Left", "Left", "Left", "Left")
    cursor_scoped_slash_palette = wait_for(
        tmux_exe,
        session,
        r"/models.*Open the model selector|Open the model selector.*models",
        "cursor-scoped slash command palette",
    )
    if "openai/gpt-5.5" in cursor_scoped_slash_palette:
        raise RuntimeError(
            "slash argument completion stayed visible after cursor moved back into the command name\n"
            f"screen:\n{cursor_scoped_slash_palette}"
        )

    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "/read sr")
    path_palette = wait_for(tmux_exe, session, r"src/main\.cpp|src/", "slash path completion palette")
    if "src/main.cpp" not in path_palette and "src/" not in path_palette:
        raise RuntimeError(f"slash path completion did not show workspace paths\nscreen:\n{path_palette}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "review @sr")
    wait_for(tmux_exe, session, r"review @sr", "file reference completion draft")
    reference_palette = wait_for(tmux_exe, session, r"@src/main\.cpp|@src/", "file reference completion palette")
    if "@src/main.cpp" not in reference_palette and "@src/" not in reference_palette:
        raise RuntimeError(f"file reference completion did not show workspace paths\nscreen:\n{reference_palette}")
    reference_row = next(
        ((index + 1, line) for index, line in enumerate(reference_palette.splitlines()) if "@src/main.cpp" in line),
        None,
    )
    if reference_row is None:
        raise RuntimeError(f"file reference completion did not expose a clickable file row\nscreen:\n{reference_palette}")
    reference_row_number, reference_row_text = reference_row
    reference_column = max(1, len(reference_row_text) - len(reference_row_text.lstrip()) + 4)
    send_literal(tmux_exe, session, f"\x1b[<0;{reference_column};{reference_row_number}M")
    clicked_reference = wait_for(
        tmux_exe, session, r"review @src/main\.cpp", "file reference completion mouse selection"
    )
    if "review @src/main.cpp" not in clicked_reference:
        raise RuntimeError(f"file reference mouse click did not update the draft\nscreen:\n{clicked_reference}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "include=@sr")
    wait_for(tmux_exe, session, r"include=@sr", "equals-delimited file reference draft")
    equals_reference_palette = wait_for(
        tmux_exe, session, r"@src/main\.cpp|@src/", "equals-delimited file reference completion palette"
    )
    if "@src/main.cpp" not in equals_reference_palette and "@src/" not in equals_reference_palette:
        raise RuntimeError(
            f"equals-delimited file reference completion did not show workspace paths\nscreen:\n{equals_reference_palette}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "include='@sr")
    wait_for(tmux_exe, session, r"include='@sr", "single-quote-delimited file reference draft")
    single_quote_reference_palette = wait_for(
        tmux_exe, session, r"@src/main\.cpp|@src/", "single-quote-delimited file reference completion palette"
    )
    if "@src/main.cpp" not in single_quote_reference_palette and "@src/" not in single_quote_reference_palette:
        raise RuntimeError(
            "single-quote-delimited file reference completion did not show workspace paths\n"
            f"screen:\n{single_quote_reference_palette}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "review @my")
    wait_for(tmux_exe, session, r"review @my", "quoted file reference draft")
    spaced_reference_palette = wait_for(
        tmux_exe, session, r'@"my folder/"', "quoted file reference completion palette"
    )
    if '@"my folder/"' not in spaced_reference_palette:
        raise RuntimeError(
            f"quoted file reference completion did not show a path with spaces\nscreen:\n{spaced_reference_palette}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "inspect src/")
    wait_for(tmux_exe, session, r"inspect src/", "normal prompt path completion draft")
    prompt_path_palette = wait_for(tmux_exe, session, r"src/main\.cpp", "normal prompt path completion palette")
    if "src/main.cpp" not in prompt_path_palette:
        raise RuntimeError(f"normal prompt path completion did not show workspace paths\nscreen:\n{prompt_path_palette}")
    path_row = next(
        ((index + 1, line) for index, line in enumerate(prompt_path_palette.splitlines()) if "src/main.cpp" in line),
        None,
    )
    if path_row is None:
        raise RuntimeError(f"normal prompt path completion did not expose a clickable file row\nscreen:\n{prompt_path_palette}")
    path_row_number, path_row_text = path_row
    path_column = max(1, len(path_row_text) - len(path_row_text.lstrip()) + 4)
    send_literal(tmux_exe, session, f"\x1b[<0;{path_column};{path_row_number}M")
    clicked_path = wait_for(tmux_exe, session, r"inspect src/main\.cpp", "normal path completion mouse selection")
    if "inspect src/main.cpp" not in clicked_path:
        raise RuntimeError(f"normal prompt path mouse click did not update the draft\nscreen:\n{clicked_path}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "ordinary")
    wait_for(tmux_exe, session, r"ordinary", "ordinary prompt word before trailing Space")
    cursor_before_space = pane_cursor_position(tmux_exe, session)
    send_keys(tmux_exe, session, "Space")
    cursor_after_space = wait_for_cursor_change(
        tmux_exe, session, cursor_before_space, "ordinary prompt cursor after trailing Space"
    )
    before_column, before_row = (int(value) for value in cursor_before_space.split(",", 1))
    after_column, after_row = (int(value) for value in cursor_after_space.split(",", 1))
    if after_row != before_row or after_column != before_column + 1:
        raise RuntimeError(
            "ordinary trailing Space did not move the cursor exactly one cell to the right "
            f"on the same row: before={cursor_before_space}, after={cursor_after_space}"
        )
    screen = assert_screen_absent_for(
        tmux_exe,
        session,
        r"@?src/main\.cpp|@?src/",
        "ordinary trailing Space opened a workspace file/path completion palette",
    )
    styled_screen = capture_styled(tmux_exe, session)
    if "ordinary " not in styled_screen:
        raise RuntimeError(
            "styled capture did not preserve the visible ordinary draft after trailing Space; "
            f"cursor before={cursor_before_space}, cursor after={cursor_after_space}\nscreen:\n{styled_screen}"
        )
    save_evidence(root, "composer-ordinary-space-no-completion", screen)

    send_keys(tmux_exe, session, "Tab")
    forced_whitespace_path_palette = wait_for(
        tmux_exe, session, r"src/main\.cpp|src/", "forced empty-token path completion after whitespace"
    )
    if "src/main.cpp" not in forced_whitespace_path_palette and "src/" not in forced_whitespace_path_palette:
        raise RuntimeError(
            "forced empty-token path completion after whitespace did not show workspace paths\n"
            f"screen:\n{forced_whitespace_path_palette}"
        )
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"ordinary", "ordinary prompt cleared after forced completion")

    send_literal(tmux_exe, session, "inspect file=src/")
    equals_path_palette = wait_for(
        tmux_exe, session, r"src/main\.cpp", "equals-delimited normal prompt path completion palette"
    )
    if "src/main.cpp" not in equals_path_palette:
        raise RuntimeError(
            f"equals-delimited normal prompt path completion did not show workspace paths\nscreen:\n{equals_path_palette}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "inspect path='src/")
    single_quote_path_palette = wait_for(
        tmux_exe, session, r"src/main\.cpp", "single-quote-delimited normal prompt path completion palette"
    )
    if "src/main.cpp" not in single_quote_path_palette:
        raise RuntimeError(
            "single-quote-delimited normal prompt path completion did not show workspace paths\n"
            f"screen:\n{single_quote_path_palette}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "main")
    send_keys(tmux_exe, session, "Tab")
    forced_path_completion = wait_for(tmux_exe, session, r"src/main\.cpp", "forced bare-token path completion")
    if "src/main.cpp" not in forced_path_completion:
        raise RuntimeError(f"forced path completion did not insert workspace path\nscreen:\n{forced_path_completion}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/read main")
    wait_for(tmux_exe, session, r"/read main", "slash-command argument path fallback draft")
    send_keys(tmux_exe, session, "Tab")
    forced_slash_argument_path = wait_for(
        tmux_exe, session, r"/read src/main\.cpp", "forced slash-command argument path fallback"
    )
    if "/read src/main.cpp" not in forced_slash_argument_path:
        raise RuntimeError(
            f"forced slash-command argument path fallback did not update the draft\nscreen:\n{forced_slash_argument_path}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/find src/*.cpp")
    wait_for(tmux_exe, session, r"/find src/\*\.cpp", "find alias command draft")
    send_keys(tmux_exe, session, "Enter")
    find_alias = wait_for(tmux_exe, session, r"(?s)find.*src/main\.cpp", "find alias command result")
    if "find" not in find_alias or "src/main.cpp" not in find_alias:
        raise RuntimeError(f"/find alias did not render glob output\nscreen:\n{find_alias}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/ls src")
    wait_for(tmux_exe, session, r"/ls src", "ls alias command draft")
    ls_palette_before_dismissal = capture(tmux_exe, session)
    send_keys(tmux_exe, session, "Escape")
    wait_for_screen_change(tmux_exe, session, ls_palette_before_dismissal, "ls palette dismissal")
    send_keys(tmux_exe, session, "Enter")
    ls_alias = wait_for(tmux_exe, session, r"(?s)ls.*main\.cpp", "ls alias command result")
    if "ls" not in ls_alias or "main.cpp" not in ls_alias:
        raise RuntimeError(f"/ls alias did not render directory output\nscreen:\n{ls_alias}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "!pwd")
    send_keys(tmux_exe, session, "Enter")
    bang_permission = wait_for(
        tmux_exe,
        session,
        r"(?s)Permission required.*bash: pwd.*risk critical",
        "bang shell critical permission",
    )
    if "Allow once" not in bang_permission or "Always" in bang_permission:
        raise RuntimeError(f"raw ! shell helper did not expose one-shot-only approval\nscreen:\n{bang_permission}")
    send_keys(tmux_exe, session, "A", "Enter")
    bang_shell = wait_for(tmux_exe, session, r"(?s)!pwd.*exit: 0", "allowed bang shell helper")
    if "Permission required" in bang_shell or "PERMISSION REQUIRED" in bang_shell:
        raise RuntimeError(f"allowed ! shell helper left its permission prompt open\nscreen:\n{bang_shell}")


    _finish_main(tmux_exe, session)


def scenario_main_permission_flow(ctx: SmokeContext) -> None:
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/bash git push origin main")
    send_keys(tmux_exe, session, "Enter")
    permission = wait_for(
        tmux_exe,
        session,
        r"(?s)Permission required.*risk critical.*reason sealed",
        "permission prompt risk metadata",
    )
    if "risk critical" not in permission or "id permreq_" not in permission or "reason sealed" not in permission:
        raise RuntimeError(f"permission prompt did not expose risk, request id, and reason metadata\nscreen:\n{permission}")
    if ("[Reject rule]" not in permission and "[DR]" not in permission) or "[Always]" in permission or "[AR]" in permission:
        raise RuntimeError(f"critical raw-shell prompt did not expose reject-rule and one-shot-only allow choices\nscreen:\n{permission}")
    permission_request_match = re.search(r"id (permreq_[A-Za-z0-9_]+)", permission)
    if not permission_request_match:
        raise RuntimeError(f"permission prompt did not expose a reusable request-id selector\nscreen:\n{permission}")
    permission_request_prefix = permission_request_match.group(1)
    save_evidence(root, "permission-prompt-risk-request", permission)
    send_keys(tmux_exe, session, "R", "Enter")
    wait_for_absent(tmux_exe, session, r"Permission required", "permission prompt denied")
    denied_card = wait_for(
        tmux_exe,
        session,
        r"(?s)permission deny.*reason command permission denied",
        "permission denial tool-card audit",
    )
    if "permission deny" not in denied_card or "reason command permission denied" not in denied_card:
        raise RuntimeError(f"permission denial did not remain visible on the tool card\nscreen:\n{denied_card}")
    save_evidence(root, "permission-denied-tool-card", denied_card)

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/permissions list")
    send_keys(tmux_exe, session, "Enter")
    remembered_rule = wait_for(
        tmux_exe,
        session,
        r'(?s)permrule_.*deny bash.*command="git push origin main"',
        "remembered permission rule listing",
    )
    if 'command="git push origin main"' not in remembered_rule or "deny bash" not in remembered_rule:
        raise RuntimeError(f"remembered exact critical-command deny rule was not listed\nscreen:\n{remembered_rule}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/bash git push origin main")
    send_keys(tmux_exe, session, "Enter")
    repeated_denial = wait_for(
        tmux_exe,
        session,
        r"(?s)/bash git push origin main.*\[x\] bash\s+error.*permission deny.*reason command permission denied",
        "remembered denial result",
    )
    if "Permission required" in repeated_denial or "PERMISSION REQUIRED" in repeated_denial:
        raise RuntimeError(f"remembered deny rule did not suppress a repeated prompt\nscreen:\n{repeated_denial}")
    send_keys(tmux_exe, session, "C-o")
    expanded_tool_details = wait_for(
        tmux_exe,
        session,
        r"(?s)request_id: permreq_.*command: <redacted one-shot command>.*inspect: /permissions audit show",
        "ctrl-o tool detail expansion",
    )
    if (
        "request_id: permreq_" not in expanded_tool_details
        or "command: <redacted one-shot command>" not in expanded_tool_details
        or "inspect: /permissions audit show" not in expanded_tool_details
        or "diagnose: /permissions diagnose" not in expanded_tool_details
    ):
        raise RuntimeError(f"Ctrl+O did not expand the visible permission tool-card details\nscreen:\n{expanded_tool_details}")
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "56", "-y", "28")
    narrow_plain_permission = wait_for(
        tmux_exe,
        session,
        r"(?s)permission_denied: command requires permission.*action: ask.*reason: command permission denied.*risk: critical.*request_id:\s*permreq_.*command: <redacted one-shot command>.*inspect: /permissions audit show\s*permreq_.*diagnose: /permissions diagnose\s*permreq_",
        "narrow plain permission detail rows",
    )
    if (
        "permission_denied: command requires permission" not in narrow_plain_permission
        or "action: ask" not in narrow_plain_permission
        or "risk: critical" not in narrow_plain_permission
        or "request_id:" not in narrow_plain_permission
        or "permreq_" not in narrow_plain_permission
        or "reason: command permission denied" not in narrow_plain_permission
        or "command: <redacted one-shot command>" not in narrow_plain_permission
        or "inspect: /permissions audit show" not in narrow_plain_permission
        or "diagnose: /permissions diagnose" not in narrow_plain_permission
    ):
        raise RuntimeError(
            f"narrow NO_COLOR permission details did not remain readable as text rows\nscreen:\n{narrow_plain_permission}"
        )
    save_evidence(root, "permission-denied-narrow-no-color", narrow_plain_permission)
    styled_narrow_permission = capture_styled(tmux_exe, session)
    if "\x1b[" in styled_narrow_permission:
        raise RuntimeError(
            f"narrow NO_COLOR permission details still captured ANSI style escapes\nscreen:\n{styled_narrow_permission}"
        )
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "120", "-y", "32")
    wait_for(tmux_exe, session, r"command: <redacted one-shot command>", "permission detail rows after resize restore")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/copy tool")
    send_keys(tmux_exe, session, "Enter")
    copied_tool = wait_for(tmux_exe, session, r"copied latest tool details to clipboard", "copy latest tool details")
    if "copied latest tool details to clipboard" not in copied_tool:
        raise RuntimeError(f"/copy tool did not report a copied tool-detail payload\nscreen:\n{copied_tool}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, f"/copy permission {permission_request_prefix}")
    send_keys(tmux_exe, session, "Enter")
    copied_permission = wait_for(
        tmux_exe,
        session,
        r"copied matching permission details to clipboard",
        "copy matching permission details",
    )
    if "copied matching permission details to clipboard" not in copied_permission:
        raise RuntimeError(
            f"/copy permission <query> did not report copied matching permission details\nscreen:\n{copied_permission}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/write src/main.cpp int changed() { return 1; }")
    send_keys(tmux_exe, session, "Enter")
    write_result = wait_for(
        tmux_exe,
        session,
        r"wrote 27 bytes|Permission required",
        "write command result for diff copy",
    )
    if "Permission required" in write_result or "PERMISSION REQUIRED" in write_result:
        send_keys(tmux_exe, session, "Tab", "Enter")
        write_result = wait_for(tmux_exe, session, r"wrote 27 bytes", "allowed write command result")
    if "wrote 27 bytes" not in write_result:
        raise RuntimeError(f"/write did not render a successful mutation tool card\nscreen:\n{write_result}")
    save_evidence(root, "write-tool-card-success", write_result)
    write_changed_details = wait_for(
        tmux_exe,
        session,
        r"changed:",
        "write changed-file detail row",
    )
    if "changed:" not in write_changed_details:
        raise RuntimeError(
            f"/write did not render the changed-file summary row in expanded tool details\nscreen:\n{write_changed_details}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/copy diff")
    send_keys(tmux_exe, session, "Enter")
    copied_diff = wait_for(tmux_exe, session, r"copied latest tool diff to clipboard", "copy latest tool diff")
    if "copied latest tool diff to clipboard" not in copied_diff:
        raise RuntimeError(f"/copy diff did not report a copied unified diff\nscreen:\n{copied_diff}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/copy diff main.cpp")
    send_keys(tmux_exe, session, "Enter")
    copied_matching_diff = wait_for(
        tmux_exe, session, r"copied matching tool diff to clipboard", "copy matching tool diff"
    )
    if "copied matching tool diff to clipboard" not in copied_matching_diff:
        raise RuntimeError(f"/copy diff <query> did not report a copied matching diff\nscreen:\n{copied_matching_diff}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/diff main.cpp")
    send_keys(tmux_exe, session, "Enter")
    visible_matching_diff = wait_for(
        tmux_exe,
        session,
        r"(?s)Matching tool diff:.*\+int changed\(\)",
        "visible matching tool diff",
    )
    if "Matching tool diff:" not in visible_matching_diff or "+int changed()" not in visible_matching_diff:
        raise RuntimeError(f"/diff <query> did not render the matching unified diff\nscreen:\n{visible_matching_diff}")
    save_evidence(root, "visible-diff-card", visible_matching_diff)

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/tool write")
    send_keys(tmux_exe, session, "Enter")
    visible_matching_tool = wait_for(
        tmux_exe,
        session,
        r"(?s)showing matching tool details.*\[\+\] write.*changed:",
        "visible matching tool details",
    )
    if "showing matching tool details" not in visible_matching_tool or "[+] write" not in visible_matching_tool or "changed:" not in visible_matching_tool:
        raise RuntimeError(f"/tool <query> did not render the matching expanded tool card\nscreen:\n{visible_matching_tool}")
    save_evidence(root, "visible-tool-details", visible_matching_tool)

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/copy tool write")
    send_keys(tmux_exe, session, "Enter")
    copied_matching_tool = wait_for(
        tmux_exe, session, r"copied matching tool details to clipboard", "copy matching tool details"
    )
    if "copied matching tool details to clipboard" not in copied_matching_tool:
        raise RuntimeError(f"/copy tool <query> did not report a copied matching tool\nscreen:\n{copied_matching_tool}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, f"/permissions audit {permission_request_prefix}")
    send_keys(tmux_exe, session, "Enter")
    permission_audit = wait_for(
        tmux_exe,
        session,
        r"(?s)request=permreq_.*<redacted one-shot command>",
        "permission audit command output",
    )
    if "request=permreq_" not in permission_audit or "<redacted one-shot command>" not in permission_audit:
        raise RuntimeError(f"permission audit command did not render the redacted denied command\nscreen:\n{permission_audit}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, f"/permissions audit summary {permission_request_prefix}")
    send_keys(tmux_exe, session, "Enter")
    permission_audit_summary = wait_for(
        tmux_exe,
        session,
        r"(?s)Permission audit summary:.*denials: [1-9].*by resolution: deny=",
        "permission audit summary command output",
    )
    if (
        "Permission audit summary:" not in permission_audit_summary
        or "denials: " not in permission_audit_summary
        or "by resolution: deny=" not in permission_audit_summary
    ):
        raise RuntimeError(
            f"permission audit summary command did not render grouped audit counts\nscreen:\n{permission_audit_summary}"
        )
    save_evidence(root, "permission-audit-summary", permission_audit_summary)

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, f"/permissions audit export {permission_request_prefix}")
    send_keys(tmux_exe, session, "Enter")
    permission_audit_export = wait_for(
        tmux_exe,
        session,
        r"(?s)<redacted one-shot command>.*```",
        "permission audit export command output",
    )
    if "```" not in permission_audit_export or "<redacted one-shot command>" not in permission_audit_export:
        raise RuntimeError(
            f"permission audit export command did not render the redacted denied command\nscreen:\n{permission_audit_export}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, f"/permissions diagnose {permission_request_prefix}")
    send_keys(tmux_exe, session, "Enter")
    permission_diagnostics = wait_for(
        tmux_exe,
        session,
        r"(?s)Recent permission denials:.*<redacted one-shot command>",
        "permission denial diagnostics command output",
    )
    if "Recent permission denials:" not in permission_diagnostics or "<redacted one-shot command>" not in permission_diagnostics:
        raise RuntimeError(
            f"permission denial diagnostics command did not explain the redacted denied command\nscreen:\n{permission_diagnostics}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, f"/permissions audit show {permission_request_prefix}")
    send_keys(tmux_exe, session, "Enter")
    permission_audit_detail = wait_for(
        tmux_exe,
        session,
        r"(?s)command: <redacted one-shot command>.*Related commands",
        "permission audit detail command output",
    )
    if "command: <redacted one-shot command>" not in permission_audit_detail or "Related commands" not in permission_audit_detail:
        raise RuntimeError(
            f"permission audit detail command did not render the redacted denied command\nscreen:\n{permission_audit_detail}"
        )


    _finish_main(tmux_exe, session)


def scenario_main_session_mgmt(ctx: SmokeContext) -> None:
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)
    # This scenario cannot rely on prior scenarios for transcript rows.
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/help")
    wait_for(tmux_exe, session, r"/help", "session-management scrollback seed draft")
    send_keys(tmux_exe, session, "Enter")
    wait_for(
        tmux_exe,
        session,
        r"page_up PageUp|model_cycle_forward|details_toggle|tree_fold_or_up|tree_unfold_or_down",
        "session-management deterministic scrollback seed output",
    )
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/name TUI smoke")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"session name set: \"TUI smoke\"", "session name command")
    send_keys(tmux_exe, session, "Up")
    arrow_scrollback = wait_for(tmux_exe, session, r"scrollback detached", "idle Up arrow transcript scrolling")
    if "│  /name TUI smoke" in arrow_scrollback:
        raise RuntimeError(
            "idle Up arrow recalled composer input history instead of only scrolling transcript history\n"
            f"screen:\n{arrow_scrollback}"
        )
    send_keys(tmux_exe, session, "Down")
    wait_for_absent(tmux_exe, session, r"scrollback detached", "idle Down arrow return to live tail")

    for index in range(1, 7):
        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, f"/new Page {index}")
        send_keys(tmux_exe, session, "Enter")
        wait_for(tmux_exe, session, rf"started session session_.*Page {index}", f"seed page session {index}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette row")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/resume.*Resume a session", "resume palette dismissed")
    send_keys(tmux_exe, session, "Enter")
    selector = wait_for(tmux_exe, session, r"Select session|Session tree", "resume session selector")
    if "session selector opened" not in selector and "Select session" not in selector:
        raise RuntimeError(f"/resume did not open the session selector\nscreen:\n{selector}")
    save_evidence(root, "session-selector", selector)
    send_literal(tmux_exe, session, "\x1b[6~")
    page_down = wait_for(tmux_exe, session, r"›\s+Page 1", "session selector page down")
    if "Page 1" not in page_down:
        raise RuntimeError(f"session selector PageDown did not jump by a page\nscreen:\n{page_down}")
    send_literal(tmux_exe, session, "\x1b[5~")
    page_up = wait_for(tmux_exe, session, r"›\s+(?:●\s+)?Page 6", "session selector page up")
    if "Page 6" not in page_up:
        raise RuntimeError(f"session selector PageUp did not jump by a page\nscreen:\n{page_up}")
    send_literal(tmux_exe, session, "TUI smoke")
    wait_for(tmux_exe, session, r"›\s+TUI smoke", "session selector query after page navigation")
    send_keys(tmux_exe, session, "C-s")
    wait_for(tmux_exe, session, r"sort name|Ctrl\+S/Ctrl\+T sort \(name\)", "session selector sort cycle")
    send_keys(tmux_exe, session, "C-n")
    named_filter = wait_for(tmux_exe, session, r"named only|med only|Ctrl\+N sho", "session selector named-only filter")
    if "TUI smoke" not in named_filter or (
        "named only" not in named_filter and "med only" not in named_filter and "Ctrl+N sho" not in named_filter
    ):
        raise RuntimeError(f"session selector named-only filter did not keep the named session visible\nscreen:\n{named_filter}")
    send_keys(tmux_exe, session, "C-p")
    path_toggle = wait_for(tmux_exe, session, r"paths hidden", "session selector path-display toggle")
    if "TUI smoke" not in path_toggle or "paths hidden" not in path_toggle:
        raise RuntimeError(f"session selector path-display toggle did not keep the named session visible\nscreen:\n{path_toggle}")
    send_keys(tmux_exe, session, "C-r")
    rename_draft = wait_for(tmux_exe, session, r"/sessions rename session_", "session selector rename draft")
    if "/sessions rename session_" not in rename_draft:
        raise RuntimeError(f"session selector Ctrl+R did not restore a rename command draft\nscreen:\n{rename_draft}")
    send_literal(tmux_exe, session, "Selector rename")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"session .* name set: \"Selector rename\"", "session selector rename command")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before label draft")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/resume.*Resume a session", "resume palette dismissed before label draft")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before label draft")
    send_literal(tmux_exe, session, "Selector rename")
    wait_for(tmux_exe, session, r"›\s+Selector rename", "resume selector filtered before label draft")
    send_literal(tmux_exe, session, "L")
    labels_draft = wait_for(tmux_exe, session, r"/sessions labels session_", "session selector Shift+L labels draft")
    if "/sessions labels session_" not in labels_draft:
        raise RuntimeError(f"session selector Shift+L did not restore a labels command draft\nscreen:\n{labels_draft}")
    send_literal(tmux_exe, session, "picker bookmark")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"session .* labels set: picker,bookmark", "session selector labels command")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/sessions picker")
    # Post-submit state refreshes the dynamic session completion catalog. Wait
    # until its argument palette is actually active before dismissing it; an
    # early Escape can race the asynchronous state refresh and leave Enter to
    # accept a session-id completion.
    wait_for(tmux_exe, session, r"›|> .*session_", "literal sessions query completion active")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"›|Search sessions", "literal sessions query completion dismissed")
    send_keys(tmux_exe, session, "Enter")
    wait_for(
        tmux_exe,
        session,
        r"(?s)Sessions:.*Selector rename.*labels=picker,bookmark",
        "session selector labels visible in tree",
    )
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before label-time toggle")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/resume.*Resume a session", "resume palette dismissed before label-time toggle")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before label-time toggle")
    send_literal(tmux_exe, session, "Selector rename")
    wait_for(tmux_exe, session, r"›\s+Selector rename", "resume selector filtered before label-time toggle")
    send_literal(tmux_exe, session, "T")
    label_time = wait_for(tmux_exe, session, r"e shown|hide label time", "session selector Shift+T label-time toggle")
    if "Selector rename" not in label_time:
        raise RuntimeError(f"session selector Shift+T lost the filtered labeled row\nscreen:\n{label_time}")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Select session|Session tree", "session selector closed after label-time toggle")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/new Archive current")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"started session session_", "new session before selector archive")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before archive")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/resume.*Resume a session", "resume palette dismissed before archive")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before archive")
    send_literal(tmux_exe, session, "Selector rename")
    wait_for(tmux_exe, session, r"›\s+Selector rename", "session selector non-current row selected")
    send_literal(tmux_exe, session, "\x1b[127;5u")
    ctrl_backspace_filtered = assert_screen_absent_for(
        tmux_exe,
        session,
        r"press Ctrl+Backspace again|press Ctrl+D again",
        "Ctrl+Backspace archive confirmation while the selector query was non-empty",
    )
    if (
        "press Ctrl+Backspace again" in ctrl_backspace_filtered
        or "press Ctrl+D again" in ctrl_backspace_filtered
    ):
        raise RuntimeError(
            "Ctrl+Backspace opened archive confirmation while the selector query was non-empty\n"
            f"screen:\n{ctrl_backspace_filtered}"
        )
    for _ in range(len("Selector rename")):
        send_literal(tmux_exe, session, "\x1b[127;2u")
    wait_for(tmux_exe, session, r"›\s+Selector rename", "session selector row selected after clearing query")
    send_literal(tmux_exe, session, "\x1b[127;5u")
    ctrl_backspace_confirmation = assert_screen_present_for(
        tmux_exe,
        session,
        r"Select session|Session tree",
        "Ctrl+Backspace first archive press closed the selector instead of waiting for confirmation",
    )
    if "Select session" not in ctrl_backspace_confirmation and "Session tree" not in ctrl_backspace_confirmation:
        raise RuntimeError(
            "Ctrl+Backspace first archive press closed the selector instead of waiting for confirmation\n"
            f"screen:\n{ctrl_backspace_confirmation}"
        )
    archived_selector_before = capture(tmux_exe, session)
    send_literal(tmux_exe, session, "\x1b[127;5u")
    wait_for_screen_change(tmux_exe, session, archived_selector_before, "selector archive completion")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"Select session|Session tree", "session selector closed after archive")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/sessions --archived Selector rename")
    send_keys(tmux_exe, session, "Enter")
    archived_sessions = wait_for(
        tmux_exe, session, r"(?s)Sessions \(including archived\):.*Selector rename", "archived session list"
    )
    if "archived" not in archived_sessions:
        raise RuntimeError(f"Archived session list did not mark the archived row\nscreen:\n{archived_sessions}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before restore")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/resume.*Resume a session", "resume palette dismissed before restore")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before restore")
    send_keys(tmux_exe, session, "C-a")
    archived_selector = wait_for(tmux_exe, session, r"archived shown", "session selector archived toggle")
    send_literal(tmux_exe, session, "Selector rename")
    archived_selector = wait_for(
        tmux_exe, session, r"(?s)›.*Selector rename.*archived|archived.*›.*Selector rename", "archived row filtered in selector"
    )
    if "archived" not in archived_selector:
        raise RuntimeError(f"session selector did not show archived session state\nscreen:\n{archived_selector}")
    send_keys(tmux_exe, session, "C-d")
    assert_screen_present_for(
        tmux_exe,
        session,
        r"›.*Selector rename.*archived|archived.*›.*Selector rename",
        "selector restore confirmation",
    )
    send_keys(tmux_exe, session, "C-d")
    wait_for_absent(
        tmux_exe,
        session,
        r"›.*Selector rename.*archived|archived.*›.*Selector rename",
        "selector restore completion",
    )
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"Select session|Session tree", "session selector closed after restore")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/sessions Selector rename")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"(?s)Sessions:.*Selector rename", "restored session visible in default list")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/name Branch parent")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"session name set: \"Branch parent\"", "branch parent session name")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/fork Branch child")
    send_keys(tmux_exe, session, "Enter")
    wait_for(
        tmux_exe,
        session,
        r"(?s)forked session session_.*name=\"Branch child\".*switched to session_",
        "forked child session before selector branch navigation",
    )
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before parent branch navigation")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(
        tmux_exe,
        session,
        r"/resume.*Resume a session",
        "resume palette dismissed before parent branch navigation",
    )
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before parent branch navigation")
    send_literal(tmux_exe, session, "\x1b[1;3D")
    wait_for(tmux_exe, session, r"opened parent branch session_", "selector alt-left opened parent branch")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/sessions Branch parent")
    send_keys(tmux_exe, session, "Enter")
    parent_active = wait_for(
        tmux_exe,
        session,
        r"(?s)Sessions:.*\* Branch parent",
        "parent branch active after selector alt-left",
    )
    if "* Branch parent" not in parent_active:
        raise RuntimeError(f"selector Alt+Left did not make the parent branch current\nscreen:\n{parent_active}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before child branch navigation")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(
        tmux_exe,
        session,
        r"/resume.*Resume a session",
        "resume palette dismissed before child branch navigation",
    )
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before child branch navigation")
    send_literal(tmux_exe, session, "\x1b[1;3C")
    wait_for(tmux_exe, session, r"opened child branch session_", "selector alt-right opened child branch")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/sessions Branch child")
    send_keys(tmux_exe, session, "Enter")
    child_active = wait_for(
        tmux_exe,
        session,
        r"(?s)Sessions:.*\* Branch child",
        "child branch active after selector alt-right",
    )
    if "* Branch child" not in child_active:
        raise RuntimeError(f"selector Alt+Right did not make the child branch current\nscreen:\n{child_active}")

    _finish_main(tmux_exe, session)


def scenario_main_paste_scrollback_attach(ctx: SmokeContext) -> None:
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)
    large_paste = "\n".join(f"line{i:02d}" for i in range(1, 12))
    send_literal(tmux_exe, session, f"\x1b[200~{large_paste}\x1b[201~")
    paste_marker = wait_for(tmux_exe, session, r"\[paste #1 \+11 lines\]", "large bracketed paste marker")
    if "line11" in paste_marker:
        raise RuntimeError(f"large paste content leaked instead of collapsing to a marker\nscreen:\n{paste_marker}")
    save_evidence(root, "large-paste-marker", paste_marker)
    send_keys(tmux_exe, session, "Left")
    send_literal(tmux_exe, session, "X")
    atomic_marker = wait_for(tmux_exe, session, r"X\[paste #1 \+11 lines\]", "large paste marker atomic left movement")
    if "linesX" in atomic_marker:
        raise RuntimeError(f"left-arrow entered the paste marker instead of jumping over it\nscreen:\n{atomic_marker}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"X\[paste #1 \+11 lines\]", "large paste marker clear")

    send_literal(tmux_exe, session, "A")
    send_literal(tmux_exe, session, f"\x1b[200~{large_paste}\x1b[201~")
    send_literal(tmux_exe, session, "B")
    wait_for(tmux_exe, session, r"A\[paste #1 \+11 lines\]B", "large paste marker forward delete draft")
    send_keys(tmux_exe, session, "C-a", "Right", "Delete")
    wait_for(tmux_exe, session, r"│  AB|^AB$", "large paste marker atomic forward delete")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"A\[paste #1 \+11 lines\]B|│  AB", "large paste marker forward delete clear")

    send_literal(tmux_exe, session, "X ")
    send_literal(tmux_exe, session, f"\x1b[200~{large_paste}\x1b[201~")
    send_literal(tmux_exe, session, " Y")
    wait_for(tmux_exe, session, r"X \[paste #1 \+11 lines\] Y", "large paste marker word draft")
    send_keys(tmux_exe, session, "C-a")
    send_keys(tmux_exe, session, "M-f", "M-f")
    send_literal(tmux_exe, session, "Z")
    wait_for(tmux_exe, session, r"X \[paste #1 \+11 lines\]Z Y", "large paste marker atomic word movement")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"X \[paste #1 \+11 lines\]Z Y", "large paste marker word draft clear")

    send_literal(tmux_exe, session, "alpha beta")
    send_literal(tmux_exe, session, "\x1b[1;3D")
    send_literal(tmux_exe, session, "Z")
    alt_left_word = wait_for(tmux_exe, session, r"alpha Zbeta", "alt-left word movement")
    if "alpha betaZ" in alt_left_word:
        raise RuntimeError(f"Alt+Left did not move to the previous word before insertion\nscreen:\n{alt_left_word}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"alpha Zbeta", "alt-left word movement clear")

    send_literal(tmux_exe, session, "alpha beta")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1b[1;3C")
    send_literal(tmux_exe, session, "Y")
    alt_right_word = wait_for(tmux_exe, session, r"alphaY beta", "alt-right word movement")
    if "Yalpha beta" in alt_right_word:
        raise RuntimeError(f"Alt+Right did not move to the next word before insertion\nscreen:\n{alt_right_word}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"alphaY beta", "alt-right word movement clear")

    send_literal(tmux_exe, session, "path/to/file")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1b[1;3C")
    send_literal(tmux_exe, session, "Z")
    punctuation_word = wait_for(tmux_exe, session, r"pathZ/to/file", "alt-right punctuation word boundary")
    if "path/to/fileZ" in punctuation_word:
        raise RuntimeError(
            f"Alt+Right skipped the path punctuation boundary instead of stopping after the first segment\nscreen:\n{punctuation_word}"
        )
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"pathZ/to/file", "alt-right punctuation word boundary clear")

    send_literal(tmux_exe, session, "one two three")
    send_keys(tmux_exe, session, "C-a", "M-f", "M-d")
    forward_word_delete = wait_for(tmux_exe, session, r"│  one three", "alt-d forward word deletion")
    if "one two three" in forward_word_delete:
        raise RuntimeError(f"Alt+D did not delete the next word\nscreen:\n{forward_word_delete}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"one three", "alt-d forward word deletion clear")

    send_literal(tmux_exe, session, "alpha beta gamma")
    send_keys(tmux_exe, session, "C-a", "M-f")
    send_literal(tmux_exe, session, "\x1b[3;3~")
    alt_delete = wait_for(tmux_exe, session, r"alpha gamma", "alt-delete forward word deletion")
    if "alpha beta gamma" in alt_delete:
        raise RuntimeError(f"Alt+Delete did not delete the next word\nscreen:\n{alt_delete}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"alpha gamma", "alt-delete forward word deletion clear")

    send_literal(tmux_exe, session, "left eraseword")
    send_literal(tmux_exe, session, "\x1b\x7f")
    send_literal(tmux_exe, session, "Z")
    alt_backspace = wait_for(tmux_exe, session, r"left Z", "alt-backspace backward word deletion")
    if "eraseword" in alt_backspace:
        raise RuntimeError(f"Alt+Backspace did not delete the previous word\nscreen:\n{alt_backspace}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"left Z", "alt-backspace backward word deletion clear")

    send_literal(tmux_exe, session, "abcXdef")
    send_keys(tmux_exe, session, "C-a", "Right", "Right", "Right", "C-d")
    ctrl_d_delete = wait_for(tmux_exe, session, r"abcdef", "ctrl-d forward character deletion")
    if "abcXdef" in ctrl_d_delete:
        raise RuntimeError(f"Ctrl+D did not delete the next character\nscreen:\n{ctrl_d_delete}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"abcdef", "ctrl-d forward deletion draft clear")

    send_literal(tmux_exe, session, "hello world")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1d")
    send_literal(tmux_exe, session, "o")
    send_literal(tmux_exe, session, "Y")
    jump_forward = wait_for(tmux_exe, session, r"hellYo world", "ctrl-bracket jump forward")
    if "Yhello world" in jump_forward:
        raise RuntimeError(f"Ctrl+] inserted instead of jumping forward\nscreen:\n{jump_forward}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"hellYo world", "ctrl-bracket jump-forward draft clear")

    send_literal(tmux_exe, session, "alpha beta gamma")
    send_literal(tmux_exe, session, "\x1b\x1d")
    send_literal(tmux_exe, session, "b")
    send_literal(tmux_exe, session, "Z")
    jump_backward = wait_for(tmux_exe, session, r"alpha Zbeta gamma", "ctrl-alt-bracket jump backward")
    if "alpha beta gammaZ" in jump_backward:
        raise RuntimeError(f"Ctrl+Alt+] inserted instead of jumping backward\nscreen:\n{jump_backward}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"alpha Zbeta gamma", "ctrl-alt-bracket jump-backward draft clear")

    send_literal(tmux_exe, session, "undo word")
    send_keys(tmux_exe, session, "C-w")
    wait_for(tmux_exe, session, r"│  undo", "ctrl-w draft before ctrl-minus undo")
    send_literal(tmux_exe, session, "\x1f")
    wait_for(tmux_exe, session, r"undo word", "ctrl-minus undo restores draft")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"undo word", "ctrl-minus undo draft clear")

    send_literal(tmux_exe, session, "/help")
    wait_for(tmux_exe, session, r"/help", "multiline scrollback seed draft")
    send_keys(tmux_exe, session, "Enter")
    wait_for(
        tmux_exe,
        session,
        r"page_up PageUp|model_cycle_forward|details_toggle|tree_fold_or_up|tree_unfold_or_down",
        "multiline scrollback seed output",
    )
    send_literal(tmux_exe, session, "\x1b[200~first\nsecond\x1b[201~")
    wait_for(tmux_exe, session, r"first.*second|first", "multiline draft before transcript scroll")
    send_literal(tmux_exe, session, "\x1b[1;129A")
    multiline_scrolled = wait_for(
        tmux_exe, session, r"scrollback detached", "multiline draft physical Ghostty arrow transcript scroll"
    )
    if "first" not in multiline_scrolled or "second" not in multiline_scrolled:
        raise RuntimeError(
            "arrow-up changed the multiline composer while scrolling the transcript\n"
            f"screen:\n{multiline_scrolled}"
        )
    send_literal(tmux_exe, session, "X")
    moved = wait_for(tmux_exe, session, r"secondX", "multiline draft cursor preserved by arrow scroll")
    if "firstX" in moved:
        raise RuntimeError(f"arrow-up moved the multiline composer cursor instead of scrolling only the transcript\nscreen:\n{moved}")
    send_literal(tmux_exe, session, "\x1b[1;129B")
    wait_for_absent(tmux_exe, session, r"scrollback detached", "multiline draft return to live tail")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"secondX", "multiline transcript-scroll draft clear")

    send_literal(tmux_exe, session, "\x1b[200~home\nend\x1b[201~")
    wait_for(tmux_exe, session, r"home.*end|home", "multiline draft before home/end cursor movement")
    send_keys(tmux_exe, session, "Home")
    send_literal(tmux_exe, session, "S")
    send_keys(tmux_exe, session, "End")
    send_literal(tmux_exe, session, "E")
    home_end = wait_for(tmux_exe, session, r"SendE", "home/end line-boundary cursor movement")
    if "homeS" in home_end:
        raise RuntimeError(f"Home edited the previous line instead of current line start\nscreen:\n{home_end}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"SendE", "home/end draft clear")

    send_literal(tmux_exe, session, "\x1b[200~join\nline\x1b[201~")
    wait_for(tmux_exe, session, r"join.*line|join", "multiline draft before ctrl-k line join")
    send_literal(tmux_exe, session, "\x1b[1;133H")
    send_keys(tmux_exe, session, "End", "C-k")
    wait_for(tmux_exe, session, r"joinline", "ctrl-home/end ctrl-k line join")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"joinline", "ctrl-k line-join draft clear")

    send_literal(tmux_exe, session, "\x1b[200~alpha\nbeta\x1b[201~")
    pasted = wait_for(tmux_exe, session, r"pasted into draft safely|alpha", "bracketed paste handling")
    if "[200~" in pasted or "[201~" in pasted:
        raise RuntimeError(f"bracketed paste markers leaked into the visible screen\nscreen:\n{pasted}")

    tmux(tmux_exe, "resize-window", "-t", session, "-x", "82", "-y", "20")
    resized = wait_for(tmux_exe, session, r"alpha|Type a message|pasted into draft safely", "resize redraw")
    if "Traceback" in resized or "assert" in resized.lower():
        raise RuntimeError(f"resize frame shows failure text\nscreen:\n{resized}")
    save_evidence(root, "resize-redraw", resized)

    send_keys(tmux_exe, session, "C-c")
    if tmux(tmux_exe, "has-session", "-t", session, check=False).returncode != 0:
        raise RuntimeError("AVA exited while clearing the bracketed-paste draft")
    wait_for_absent(tmux_exe, session, r"alpha|beta", "draft clear before quit")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/help")
    wait_for(tmux_exe, session, r"/help", "help draft before mouse wheel scroll")
    send_keys(tmux_exe, session, "Enter")
    wait_for(
        tmux_exe,
        session,
        r"page_up PageUp|model_cycle_forward|details_toggle|tree_fold_or_up|tree_unfold_or_down|models_clear_all|models_reorder_down",
        "long help output before mouse wheel scroll",
    )
    send_literal(tmux_exe, session, "draft stays while scrolling")
    wait_for(tmux_exe, session, r"draft stays while scrolling", "draft before transcript-only scrolling")
    send_literal(tmux_exe, session, "\x1b[<64;4;6M")
    wheel_scrolled = wait_for(tmux_exe, session, r"scrollback detached", "raw SGR mouse wheel scrollback")
    if "scrollback detached" not in wheel_scrolled or "draft stays while scrolling" not in wheel_scrolled:
        raise RuntimeError(
            "raw SGR mouse wheel changed the composer instead of only scrolling transcript history\n"
            f"screen:\n{wheel_scrolled}"
        )
    send_literal(tmux_exe, session, "\x1b[<65;4;6M")
    wheel_tail = wait_for_absent(tmux_exe, session, r"scrollback detached", "raw SGR mouse wheel return to live tail")
    if "draft stays while scrolling" not in wheel_tail:
        raise RuntimeError(f"mouse wheel return to live tail changed the composer draft\nscreen:\n{wheel_tail}")
    send_literal(tmux_exe, session, "\x1b[1;129A")
    arrow_scrolled = wait_for(
        tmux_exe, session, r"scrollback detached", "physical Ghostty Up arrow transcript scrollback with Num Lock"
    )
    if "draft stays while scrolling" not in arrow_scrolled:
        raise RuntimeError(
            "physical Up arrow recalled composer history instead of only scrolling transcript history\n"
            f"screen:\n{arrow_scrolled}"
        )
    send_literal(tmux_exe, session, "\x1b[1;129B")
    arrow_tail = wait_for_absent(
        tmux_exe, session, r"scrollback detached", "physical Ghostty Down arrow return to live tail with Num Lock"
    )
    if "draft stays while scrolling" not in arrow_tail:
        raise RuntimeError(f"physical Down arrow changed the composer draft\nscreen:\n{arrow_tail}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"draft stays while scrolling", "scrolling regression draft clear")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/att")
    attach_palette = wait_for(tmux_exe, session, r"/attach.*Attach an image", "attach command palette")
    if "/attach" not in attach_palette or "Attach an image" not in attach_palette:
        raise RuntimeError(f"/attach did not appear in the slash palette\nscreen:\n{attach_palette}")
    save_evidence(root, "slash-attach-palette", attach_palette)
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Attach an image", "attach palette dismissed")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/attach screen.png")
    wait_for(tmux_exe, session, r"/attach screen\.png", "attach image draft")
    send_keys(tmux_exe, session, "Enter")
    attached_image = wait_for(
        tmux_exe, session, r"attached image.*screen\.png|screen\.png.*next prompt", "attached image pending row"
    )
    if (
        "attached image" not in attached_image
        or "screen.png" not in attached_image
        or "preview text-only" not in attached_image
        or "next prompt" not in attached_image
    ):
        raise RuntimeError(f"/attach did not import and queue the image visibly\nscreen:\n{attached_image}")
    save_evidence(root, "attachment-text-fallback", attached_image)
    send_keys(tmux_exe, session, "C-v")
    clipboard_image = wait_for(
        tmux_exe,
        session,
        r"attached clipboard image.*clipboard image|clipboard image.*next prompt",
        "Ctrl+V clipboard image pending row",
    )
    if (
        "attached clipboard image" not in clipboard_image
        or "clipboard image" not in clipboard_image
        or "preview text-only" not in clipboard_image
        or "next prompt" not in clipboard_image
    ):
        raise RuntimeError(f"Ctrl+V did not import and queue the clipboard image visibly\nscreen:\n{clipboard_image}")

    send_keys(tmux_exe, session, "C-d")
    wait_for_session_exit(tmux_exe, session)


SCENARIO_HANDLERS = {
    "suspend_resume": scenario_suspend_resume,
    "keybind_conflict": scenario_keybind_conflict,
    "theme_env": scenario_theme_env,
    "theme_persisted": scenario_theme_persisted,
    "active_run": scenario_active_run,
    "restore_followup": scenario_restore_followup,
    "main_startup_trust_keybinds": scenario_main_startup_trust_keybinds,
    "main_models_selectors": scenario_main_models_selectors,
    "main_editor_input": scenario_main_editor_input,
    "main_slash_completions": scenario_main_slash_completions,
    "main_permission_flow": scenario_main_permission_flow,
    "main_session_mgmt": scenario_main_session_mgmt,
    "main_paste_scrollback_attach": scenario_main_paste_scrollback_attach,
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", required=True, choices=SCENARIOS)
    parser.add_argument("--ava", required=True)
    parser.add_argument("--fake-provider", required=True)
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
    if not ava_exe.exists():
        raise RuntimeError(f"AVA executable does not exist: {ava_exe}")
    if not fake_provider_exe.exists():
        raise RuntimeError(f"fake provider executable does not exist: {fake_provider_exe}")

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
