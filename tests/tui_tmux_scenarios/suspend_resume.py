"""The tmux TUI smoke scenario for suspend resume."""

from __future__ import annotations

from tui_smoke_helpers import (
    SmokeContext,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_absent,
    wait_for_pane_command,
    wait_for_session_exit,
)


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
