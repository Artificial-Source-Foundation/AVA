"""The tmux TUI smoke scenario for keybind conflict."""

from __future__ import annotations

from tui_smoke_helpers import SmokeContext, tmux, wait_for


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
