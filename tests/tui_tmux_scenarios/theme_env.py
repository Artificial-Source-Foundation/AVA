"""The tmux TUI smoke scenario for theme env."""

from __future__ import annotations

from tui_smoke_helpers import (
    SmokeContext,
    send_keys,
    send_literal,
    tmux,
    wait_for,
)


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
