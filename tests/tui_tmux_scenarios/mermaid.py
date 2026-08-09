"""Opt-in private real-terminal Mermaid projection and hot-reload coverage."""

from __future__ import annotations

import json

from tui_smoke_helpers import (
    SmokeContext,
    capture,
    save_evidence,
    send_keys,
    send_literal,
    wait_for,
    wait_for_absent,
    wait_for_session_exit,
)
from .common import _wait_for_normal_turn_request_count


def _display_document(ctx: SmokeContext, count_file: str, enabled: bool) -> str:
    return (
        json.dumps(
            {
                "mermaid": {
                    "enabled": enabled,
                    "argv": [
                        str(ctx.fake_mermaid_helper_exe),
                        "--count",
                        count_file,
                        "argument",
                        "TMUX_MERMAID_RENDERED_LITERAL",
                    ],
                }
            }
        )
        + "\n"
    )


def scenario_mermaid(ctx: SmokeContext) -> None:
    count_file = ctx.root / "mermaid-helper-count"
    display_file = ctx.active_ava_config / "display.json"
    display_file.write_text(_display_document(ctx, str(count_file), False), encoding="utf-8")
    display_file.chmod(0o600)

    provider = ctx.start_fake_provider("mermaid", delay_ms=20, scenario="mermaid")
    command = ctx.fake_provider_command(
        provider,
        home=ctx.active_home,
        config=ctx.active_config,
        state=ctx.active_state,
        data=ctx.active_data,
    )
    session = ctx.session_name("mermaid")
    ctx.launch_ava(session, workspace=ctx.active_workspace, command=command, width=100, height=26)
    wait_for(ctx.tmux, session, r"Type a message|live session", "Mermaid initial frame")

    send_literal(ctx.tmux, session, "render the private Mermaid fixture")
    send_keys(ctx.tmux, session, "Enter")
    _wait_for_normal_turn_request_count(provider.request_log, 1, "Mermaid provider request")
    disabled = wait_for(ctx.tmux, session, r"TMUX_MERMAID_SOURCE_A-->B", "disabled raw Mermaid fence")
    if count_file.exists():
        raise RuntimeError(f"disabled Mermaid unexpectedly launched the helper\nscreen:\n{disabled}")

    display_file.write_text(_display_document(ctx, str(count_file), True), encoding="utf-8")
    display_file.chmod(0o600)
    enabled = wait_for(ctx.tmux, session, r"TMUX_MERMAID_RENDERED_LITERAL", "enabled Mermaid helper projection", timeout=10.0)
    enabled = wait_for_absent(ctx.tmux, session, r"TMUX_MERMAID_SOURCE_A-->B|```mermaid", "projected Mermaid source hidden")
    if not count_file.exists() or count_file.read_text(encoding="utf-8") != "x":
        raise RuntimeError(f"Mermaid helper did not run exactly once: {count_file}\nscreen:\n{enabled}")
    save_evidence(ctx.root, "mermaid-enabled-projection", enabled)

    send_literal(ctx.tmux, session, "/search TMUX_MERMAID_RENDERED_LITERAL")
    send_keys(ctx.tmux, session, "Enter")
    search = wait_for(ctx.tmux, session, r"Transcript search.*1 item", "projected Mermaid transcript search")
    if "assistant" not in search:
        raise RuntimeError(f"projected Mermaid output was not searchable as assistant presentation\nscreen:\n{search}")
    send_keys(ctx.tmux, session, "Escape")
    wait_for(ctx.tmux, session, r"TMUX_MERMAID_RENDERED_LITERAL", "Mermaid search viewport restoration")

    display_file.write_text(_display_document(ctx, str(count_file), False), encoding="utf-8")
    display_file.chmod(0o600)
    fallback = wait_for(ctx.tmux, session, r"TMUX_MERMAID_SOURCE_A-->B", "disabled hot-reload raw fence fallback", timeout=10.0)
    fallback = wait_for_absent(ctx.tmux, session, r"TMUX_MERMAID_RENDERED_LITERAL", "disabled hot-reload projection removed")
    if count_file.read_text(encoding="utf-8") != "x":
        raise RuntimeError(f"disable reload unexpectedly launched the Mermaid helper again\nscreen:\n{fallback}")
    save_evidence(ctx.root, "mermaid-disabled-fallback", fallback)

    send_keys(ctx.tmux, session, "C-d")
    wait_for_session_exit(ctx.tmux, session)
    provider.stop()
    provider_error = provider.stderr_path.read_text(encoding="utf-8", errors="replace")
    if provider_error:
        raise RuntimeError(f"Mermaid fake provider reported an error:\n{provider_error}")
