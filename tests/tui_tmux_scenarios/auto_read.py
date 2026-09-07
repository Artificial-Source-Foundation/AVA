"""Session Auto-read controls, including pending prompts and an active provider turn."""

from __future__ import annotations

import platform
import re
import time

from tui_smoke_helpers import SmokeContext, capture, save_evidence, send_keys, send_literal, wait_for, wait_for_absent
from .common import _finish_main


def scenario_auto_read(ctx: SmokeContext) -> None:
    session = ctx.session_name("auto-read")
    provider = ctx.start_fake_provider("auto-read", delay_ms=0, scenario="text-three-delayed-first")
    command = ctx.pane_command(home=ctx.restore_home, config=ctx.restore_config,
                               state=ctx.restore_state, data=ctx.restore_data,
                               extra={"MOONSHOT_API_KEY": "test-key", "MOONSHOT_BASE_URL": f"http://127.0.0.1:{provider.port}",
                                      "AVA_SESSION_TITLES": "off", "NO_COLOR": "1"})
    ctx.launch_ava(session, workspace=ctx.restore_workspace, command=command, width=120, height=32)
    wait_for(ctx.tmux, session, r"Type a message", "Auto-read startup")
    outside = ctx.restore_home / "outside-auto-read.txt"
    outside.write_text("OUTSIDE-READ-OK\n", encoding="utf-8")

    def submit(text: str, expected: str) -> str:
        send_literal(ctx.tmux, session, text)
        send_keys(ctx.tmux, session, "Enter")
        time.sleep(0.15)
        if re.search(r"│  " + re.escape(text) + r"\s*$", capture(ctx.tmux, session), re.MULTILINE):
            send_keys(ctx.tmux, session, "Enter")
        return wait_for(ctx.tmux, session, expected, text)

    def close_panel() -> None:
        if "Command /" in capture(ctx.tmux, session):
            send_keys(ctx.tmux, session, "Escape")
            wait_for_absent(ctx.tmux, session, r"Command /", "Auto-read command output closed")

    submit(f"/read {outside}", r"Permission required")
    send_keys(ctx.tmux, session, "F7")
    approved = wait_for(ctx.tmux, session, r"(?s)Command /read.*OUTSIDE-READ-OK", "pending read released by Auto-read")
    save_evidence(ctx.root, "auto-read-pending-approved", approved)
    close_panel()
    submit("/permissions default", r"Auto-read off")
    submit(f"/read {outside}", r"Permission required")
    send_keys(ctx.tmux, session, "d")
    wait_for(ctx.tmux, session, r"Command /read", "read denied after disabling Auto-read")
    close_panel()
    submit("/permissions read-only", r"Auto-read on")
    submit(f"/read {outside}", r"(?s)Command /read.*OUTSIDE-READ-OK")
    close_panel()
    submit(f"/ls {ctx.restore_home}", r"(?s)Command /ls.*outside-auto-read")
    close_panel()

    submit(f"/write {outside} MUST-NOT-WRITE", r"Permission required")
    send_keys(ctx.tmux, session, "F7")
    wait_for(ctx.tmux, session, r"(?s)Permission required.*Auto-read off", "toggle off preserves pending write")
    send_keys(ctx.tmux, session, "F7")
    pending_write = wait_for(ctx.tmux, session, r"(?s)Permission required.*Auto-read on", "toggle on preserves pending write")
    save_evidence(ctx.root, "auto-read-write-still-pending", pending_write)
    send_keys(ctx.tmux, session, "d")
    wait_for(ctx.tmux, session, r"Command /write", "write denied")
    close_panel()
    if outside.read_text(encoding="utf-8") != "OUTSIDE-READ-OK\n":
        raise RuntimeError("Auto-read changed a file without write approval")

    if platform.system() == "Darwin":
        shell_command = "/bash /bin/echo AVA-AUTO-READ-SHELL-OK"
        pending = submit(shell_command, r"(?s)Permission required.*risk critical")
        if "Always allow" in pending or "Allow session" in pending:
            raise RuntimeError("Auto-read exposed reusable uncontained command approval")
        send_keys(ctx.tmux, session, "F7")
        wait_for(ctx.tmux, session, r"(?s)Permission required.*Auto-read off", "CriticalAsk stays pending on toggle")
        send_keys(ctx.tmux, session, "F7")
        wait_for(ctx.tmux, session, r"(?s)Permission required.*Auto-read on", "Auto-read cannot release CriticalAsk")
        send_keys(ctx.tmux, session, "a")
        wait_for(ctx.tmux, session, r"(?s)Command /bash.*AVA-AUTO-READ-SHELL-OK", "explicit one-shot shell approval")
        close_panel()
        submit(shell_command, r"(?s)Permission required.*risk critical")
        send_keys(ctx.tmux, session, "d")
        denied = wait_for(ctx.tmux, session, r"(?s)Command /bash.*command permission denied", "repeated command requires new approval")
        save_evidence(ctx.root, "auto-read-critical-denied", denied)
        close_panel()

    rule_command = f'/permissions add action=deny operation=read path={outside} reason="keep this file denied"'
    submit(rule_command, r"Command /permissions")
    close_panel()
    denied = submit(f"/read {outside}", r"(?s)Command /read.*denied")
    if "Permission required" in denied or "OUTSIDE-READ-OK" in denied:
        raise RuntimeError("Auto-read bypassed a persistent Deny")
    close_panel()
    secret = ctx.restore_home / ".env"
    secret.write_text("SYNTHETIC-DO-NOT-READ\n", encoding="utf-8")
    denied = submit(f"/read {secret}", r"(?s)Command /read.*secret file")
    if "SYNTHETIC-DO-NOT-READ" in denied:
        raise RuntimeError("Auto-read bypassed hard policy")
    close_panel()

    send_literal(ctx.tmux, session, "held permission-toggle test")
    send_keys(ctx.tmux, session, "Enter")
    provider.wait_for_request(0, "active provider request before toggling")
    send_keys(ctx.tmux, session, "F7")
    wait_for(ctx.tmux, session, r"Auto-read off", "shortcut works during provider turn")
    during = submit("/permissions read-only", r"Auto-read on")
    save_evidence(ctx.root, "auto-read-mid-turn", during)
    if provider.request_log.read_text(encoding="utf-8").count("--- request ") != 1:
        raise RuntimeError("permission toggle unexpectedly issued a provider request")
    provider.release_request(0)
    wait_for_absent(ctx.tmux, session, r"\[~\] responding|Esc stop", "provider run completed")
    submit("/new", r"started session")
    close_panel()
    next_outside = ctx.restore_home / "new-session-auto-read.txt"
    next_outside.write_text("NEW-SESSION-READ\n", encoding="utf-8")
    submit(f"/read {next_outside}", r"Permission required")
    send_keys(ctx.tmux, session, "d")
    wait_for(ctx.tmux, session, r"Command /read", "new session defaults to normal read prompts")

    enabled_session = ctx.session_name("auto-read-startup")
    ctx.launch_ava(enabled_session, workspace=ctx.workspace, command=ctx.main_pane_command() + " --allow read-only", width=120, height=32)
    _finish_main(ctx.tmux, session)
    screen = wait_for(ctx.tmux, enabled_session, r"Build · Auto-read", "CLI flag enables the interactive default")
    save_evidence(ctx.root, "auto-read-startup-flag", screen)
    send_keys(ctx.tmux, enabled_session, "F7")
    wait_for(ctx.tmux, enabled_session, r"Auto-read off", "CLI default remains switchable")
    _finish_main(ctx.tmux, enabled_session)
