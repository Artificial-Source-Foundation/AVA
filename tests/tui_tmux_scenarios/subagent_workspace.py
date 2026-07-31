"""The tmux TUI smoke scenario for the live read-only subagent workspace."""

from __future__ import annotations

import json
import re
import time

from tui_smoke_helpers import (
    SmokeContext,
    save_evidence,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_absent,
    wait_for_request_count,
    wait_for_session_exit,
)


def _assert_workspace_safe(screen: str, label: str, *, parent_card: bool = False) -> None:
    forbidden = ["job_", "session_", "/tmp/", "<task", "</task", "call_task_live"]
    if parent_card:
        forbidden.extend(["arguments provided", "Inspect delegated fixture"])
    leaked = [value for value in forbidden if value in screen]
    if leaked:
        raise RuntimeError(f"{label} leaked hidden backend data {leaked!r}\nscreen:\n{screen}")
    if "\x1b" in screen or any(ord(character) < 32 and character != "\n" for character in screen):
        raise RuntimeError(f"{label} contained ESC or unexpected C0 controls\nscreen:\n{screen}")
    if re.search(r"(?:child|parent)[-_ ]session[-_ ]id", screen, re.IGNORECASE):
        raise RuntimeError(f"{label} exposed session identity metadata\nscreen:\n{screen}")


def scenario_subagent_workspace(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    session = ctx.session_name("subagent-workspace")
    marker_directory = ctx.root / "subagent-workspace-markers"
    marker_directory.mkdir(mode=0o700)

    models_path = ctx.active_ava_config / "models.json"
    models = json.loads(models_path.read_text(encoding="utf-8"))
    models["models"][0]["supports_tools"] = True
    models_path.write_text(json.dumps(models) + "\n", encoding="utf-8")

    provider = ctx.start_fake_provider(
        "subagent-workspace",
        delay_ms=0,
        scenario="subagent-workspace",
        target=marker_directory,
    )
    command = ctx.fake_provider_command(
        provider,
        home=ctx.active_home,
        config=ctx.active_config,
        state=ctx.active_state,
        data=ctx.active_data,
    )
    ctx.launch_ava(session, workspace=ctx.active_workspace, command=command, width=100, height=24)
    wait_for(tmux_exe, session, r"Type a message|live session", "subagent workspace initial frame")

    send_literal(tmux_exe, session, "start the live delegated workspace fixture")
    send_keys(tmux_exe, session, "Enter")
    wait_for_request_count(provider.request_log, 2, "background task and parent continuation requests", timeout=12.0)
    wait_for(tmux_exe, session, r"Esc stop.*type a follow-up|type a follow-up", "active parent run before /jobs")

    # Trailing ASCII whitespace must still open the exact active /jobs selector.
    send_literal(tmux_exe, session, "/jobs ")
    send_keys(tmux_exe, session, "Enter")
    selector = wait_for(tmux_exe, session, r"Search subagents", "live subagent selector during active parent run")
    if "Running" not in selector or "Background" not in selector:
        raise RuntimeError(f"live subagent selector omitted running/background metadata\nscreen:\n{selector}")
    _assert_workspace_safe(selector, "live subagent selector")
    save_evidence(ctx.root, "subagent-workspace-selector", selector)

    send_keys(tmux_exe, session, "Enter")
    live = wait_for(tmux_exe, session, r"Inspect delegated fixture", "committed child user message in workspace")
    if "Live workspace audit" not in live or "Type a message" in live or "Esc stop" in live:
        raise RuntimeError(f"live child workspace retained parent composer chrome\nscreen:\n{live}")
    if "Parent continued after background start" in live:
        raise RuntimeError(f"live child workspace rendered parent transcript output\nscreen:\n{live}")
    _assert_workspace_safe(live, "live child workspace")
    save_evidence(ctx.root, "subagent-workspace-live", live)

    # Active-run Escape is owned by the workspace and must not stop the parent.
    send_keys(tmux_exe, session, "Escape")
    escaped = wait_for(tmux_exe, session, r"Search subagents", "workspace Escape back to selector")
    if "stop requested" in escaped:
        raise RuntimeError(f"workspace Escape stopped the active parent run\nscreen:\n{escaped}")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Inspect delegated fixture", "workspace reopened without stopping parent")

    marker_directory.joinpath("release-live").write_text("release\n", encoding="utf-8")
    terminal = wait_for(
        tmux_exe,
        session,
        r"Committed child answer\.",
        "committed child assistant update and terminal freeze",
        timeout=12.0,
    )
    deadline = time.monotonic() + 12.0
    requests = ""
    while time.monotonic() < deadline:
        requests = provider.request_log.read_text(encoding="utf-8", errors="replace")
        if "Tool call (job): arguments_json=" in requests:
            break
        time.sleep(0.05)
    else:
        raise RuntimeError(f"parent did not issue the expected model job-list poll\nrequests:\n{requests}")
    if "Completed" not in terminal or "Parent continued after background start" in terminal:
        raise RuntimeError(f"terminal child workspace was not frozen independently of parent output\nscreen:\n{terminal}")
    _assert_workspace_safe(terminal, "terminal child workspace")
    save_evidence(ctx.root, "subagent-workspace-terminal", terminal)

    send_keys(tmux_exe, session, "Escape")
    wait_for(tmux_exe, session, r"Search subagents", "terminal workspace returned to selector")
    send_keys(tmux_exe, session, "Escape")
    restored = wait_for_absent(tmux_exe, session, r"Search subagents", "parent view restored after workspace close", timeout=12.0)
    if "Parent continued after background start" not in restored:
        raise RuntimeError(f"restored parent view omitted its own assistant output\nscreen:\n{restored}")
    if "Live workspace audit" not in restored:
        raise RuntimeError(f"restored parent view omitted specialized task card title\nscreen:\n{restored}")
    if not re.search(r"\bGeneral\b", restored):
        raise RuntimeError(f"restored parent view omitted specialized task card type\nscreen:\n{restored}")
    if re.search(r"[~+]\s+job\s+·\s+(?:list|status|wait|running)|job\s+·\s+(?:list|status|wait|running)", restored, re.IGNORECASE):
        raise RuntimeError(f"restored parent view exposed routine job polling plumbing\nscreen:\n{restored}")
    _assert_workspace_safe(restored, "restored parent view", parent_card=True)
    send_keys(tmux_exe, session, "C-d")
    wait_for_session_exit(tmux_exe, session)
    tmux(tmux_exe, "kill-session", "-t", session, check=False)
