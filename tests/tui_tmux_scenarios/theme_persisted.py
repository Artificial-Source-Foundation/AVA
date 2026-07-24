"""The tmux TUI smoke scenario for theme persisted."""

from __future__ import annotations

from tui_smoke_helpers import (
    SmokeContext,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_absent,
)


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
