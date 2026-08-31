"""The tmux TUI smoke scenario for restore followup."""

from __future__ import annotations

from tui_smoke_helpers import (
    SmokeContext,
    save_evidence,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_session_exit,
)
from .common import _assert_normal_turn_request_count_stays


def scenario_restore_followup(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    root = ctx.root
    restore_workspace = ctx.restore_workspace
    restore_active_session = ctx.session_name("restore")
    restore_provider = ctx.start_fake_provider("restore", delay_ms=0, scenario="text-three-delayed-first")
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
    restore_provider.wait_for_request(0, "active-run restore first provider request")
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
    restore_provider.release_request(0)
    wait_for(
        tmux_exe,
        restore_active_session,
        r"headless active prompt complete",
        "active-run restore original prompt completion",
        timeout=12.0,
    )
    restore_log = _assert_normal_turn_request_count_stays(
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
