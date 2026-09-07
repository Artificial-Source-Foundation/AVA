"""Review, undo, attention requests, and selective queue editing in a real TTY."""

from __future__ import annotations

import re
import shlex
import time

from tui_smoke_helpers import SmokeContext, capture, save_evidence, send_keys, send_literal, tmux, wait_for, wait_for_absent


def scenario_lean_features(ctx: SmokeContext) -> None:
    session = ctx.session_name("lean-features")
    provider = ctx.start_fake_provider("lean-features", delay_ms=0, scenario="text-three-delayed-first")
    command = ctx.fake_provider_command(provider, home=ctx.restore_home, config=ctx.restore_config,
                                        state=ctx.restore_state, data=ctx.restore_data)
    ctx.launch_ava(session, workspace=ctx.restore_workspace, command=command, width=110, height=28)
    wait_for(ctx.tmux, session, r"Type a message", "lean feature startup")
    raw_output = ctx.root / "attention-output.bin"
    tmux(ctx.tmux, "pipe-pane", "-t", session, "-o", "cat > " + shlex.quote(str(raw_output)))

    def submit(text: str, expected: str) -> str:
        send_literal(ctx.tmux, session, text)
        send_keys(ctx.tmux, session, "Enter")
        time.sleep(0.15)
        if re.search(r"│  " + re.escape(text) + r"\s*$", capture(ctx.tmux, session), re.MULTILINE):
            send_keys(ctx.tmux, session, "Enter")
        return wait_for(ctx.tmux, session, expected, text)

    def close_panel() -> None:
        send_keys(ctx.tmux, session, "Escape")
        wait_for_absent(ctx.tmux, session, r"Command /|Recorded changes", "panel dismissed before typing")

    original = ctx.restore_workspace / "review-one.txt"
    original.write_text("pre-existing user work\n", encoding="utf-8")
    submit("/write review-one.txt AVA changed this", r"Command /write")
    close_panel()
    submit("/write review-two.txt AVA created this", r"Command /write")
    close_panel()
    review = submit("/diff all", r"Recorded changes 1/2")
    save_evidence(ctx.root, "lean-review-first", review)
    send_literal(ctx.tmux, session, "m")
    wait_for(ctx.tmux, session, r"Recorded changes 2/2", "mark reviewed and next file")
    send_literal(ctx.tmux, session, "p")
    marked = wait_for(ctx.tmux, session, r"\[reviewed\].*review-one", "retained review mark")
    save_evidence(ctx.root, "lean-review-marked", marked)
    close_panel()
    preview = submit("/undo", r"Undo preview: last AVA editing turn")
    save_evidence(ctx.root, "lean-undo-preview", preview)
    if not (ctx.restore_workspace / "review-two.txt").exists():
        raise RuntimeError("undo preview mutated the workspace")
    close_panel()
    restored = submit("/undo --confirm", r"Restored 1 files")
    save_evidence(ctx.root, "lean-undo-confirmed", restored)
    if (ctx.restore_workspace / "review-two.txt").exists() or original.read_text(encoding="utf-8") != "AVA changed this":
        raise RuntimeError("undo failed to restore only the latest editing turn")
    close_panel()
    submit("/notify on", r"Terminal notifications on")

    send_literal(ctx.tmux, session, "first held prompt")
    send_keys(ctx.tmux, session, "Enter")
    provider.wait_for_request(0, "held request for queue editing")
    for message in ("REMOVE-ME", "EDIT-ME"):
        send_literal(ctx.tmux, session, message)
        send_literal(ctx.tmux, session, "\x1b\r")
        wait_for(ctx.tmux, session, re.escape(message) + r".*|follow-up queued", "queue " + message)
    queued = submit("/queue", r"Pending messages")
    if "Ctrl+D remove" not in queued:
        raise RuntimeError("pending-message picker omitted its removal shortcut")
    save_evidence(ctx.root, "lean-queue-picker", queued)
    send_keys(ctx.tmux, session, "C-d")
    removed = wait_for(ctx.tmux, session, r"Pending messages", "queue removal")
    # Match only the picker after its backend callback has refreshed it.
    wait_for_absent(ctx.tmux, session, r"›\s+REMOVE-ME\s+Follow-up", "removed queue item")
    save_evidence(ctx.root, "lean-queue-removed", capture(ctx.tmux, session))
    send_keys(ctx.tmux, session, "Enter")
    wait_for(ctx.tmux, session, r"│  EDIT-ME", "selected queued message restored to composer")
    send_literal(ctx.tmux, session, " REVISED")
    save_evidence(ctx.root, "lean-queue-edited-draft", capture(ctx.tmux, session))
    send_literal(ctx.tmux, session, "\x1b\r")
    provider.release_request(0)
    provider.wait_for_request(1, "revised queued message delivery")
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if raw_output.exists() and b"\x1b]9;AVA finished\x1b\\" in raw_output.read_bytes():
            break
        time.sleep(0.05)
    else:
        raise RuntimeError("completed run did not emit the enabled terminal attention request")
    requests = provider.request_log.read_text(encoding="utf-8")
    if "REMOVE-ME" in requests or "EDIT-ME REVISED" not in requests or "Local file undo requested by the user" not in requests:
        raise RuntimeError("queue delivery or model-visible undo receipt did not match user actions")
    save_evidence(ctx.root, "lean-attention-request", "Observed fixed OSC 9 completion request; delivery depends on terminal support.")
    submit("/notify off", r"Terminal notifications off")
