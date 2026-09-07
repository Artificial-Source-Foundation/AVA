"""Native Mac CriticalAsk is human-only and never disclosed to the reviewer."""
from __future__ import annotations

import http.server
import json
import platform
import threading
import time

from tui_smoke_helpers import capture, save_evidence, send_keys, send_literal, wait_for, wait_for_absent
from .common import _finish_main


def scenario_command_review(ctx):
    if platform.system() != "Darwin":
        raise RuntimeError("command_review exercises native macOS CriticalAsk")
    requests = []
    state = {"kind": "approve", "delay": 0.0}

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass

        def do_POST(self):
            payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            requests.append(payload)
            kind, delay = state["kind"], state["delay"]
            time.sleep(delay)
            text = json.dumps({"does": "Prints a short message in your terminal.", "risk": "low" if kind == "approve" else "high",
                               "recommendation": kind, "why": "Approve only if you want that message printed."})
            if kind == "malformed":
                text = "not a valid review"
            body = json.dumps({"choices": [{"message": {"role": "assistant", "content": text}, "finish_reason": "stop"}]}).encode()
            try:
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    session = ctx.session_name("command-review")
    models = {"default_provider": "airouter", "default_model": "Qwen3.8", "models": [
        {"provider": "airouter", "id": "Qwen3.8", "api_family": "openai_chat_completions", "context_window_tokens": 262144,
         "supports_reasoning": True, "reasoning_levels": ["none", "low"], "reasoning_format": "reasoning_content",
         "compatibility_quirks": ["reasoning_effort"]}]}
    ctx.restore_ava_config.joinpath("models.json").write_text(json.dumps(models))
    ctx.restore_ava_config.joinpath("providers.json").write_text(json.dumps({"version": 1, "providers": [
        {"id": "airouter", "display_name": "Review fixture", "protocol": "openai_chat_completions", "auth": "none",
         "base_url": f"http://127.0.0.1:{server.server_port}"}]}))
    command = ctx.pane_command(home=ctx.restore_home, config=ctx.restore_config, state=ctx.restore_state, data=ctx.restore_data,
                               extra={"AVA_SESSION_TITLES": "off", "NO_COLOR": "1"})
    try:
        ctx.launch_ava(session, workspace=ctx.restore_workspace, command=command, width=120, height=32)
        wait_for(ctx.tmux, session, r"Type a message", "review startup")

        def submit(text, pattern):
            send_literal(ctx.tmux, session, text)
            send_keys(ctx.tmux, session, "Enter")
            return wait_for(ctx.tmux, session, pattern, text)

        def close_panel():
            if "Command /" in capture(ctx.tmux, session):
                send_keys(ctx.tmux, session, "Escape")
                wait_for_absent(ctx.tmux, session, r"Command /", "command panel closed")

        submit("/permissions review on", "Qwen review on")
        pending = submit("/bash /bin/echo AVA-QWEN-OK", "Permission required")
        if "Permission required" not in pending or "risk critical" not in pending or "Allow session" in pending:
            raise RuntimeError("favorable review changed CriticalAsk authority")
        if requests:
            raise RuntimeError("Critical command was disclosed to reviewer")
        save_evidence(ctx.root, "review-critical-pending", pending)
        send_keys(ctx.tmux, session, "e", "e")
        time.sleep(0.2)
        if requests:
            raise RuntimeError("Explain disclosed an ineligible command")
        send_keys(ctx.tmux, session, "a")
        wait_for(ctx.tmux, session, r"(?s)Command /bash.*AVA-QWEN-OK", "human-approved command executes")
        close_panel()

        submit("/bash /bin/echo AVA-QWEN-DENY", "Permission required")
        send_keys(ctx.tmux, session, "d")
        denied = wait_for(ctx.tmux, session, r"Command /bash", "human denial completes")
        save_evidence(ctx.root, "review-denied", denied)
        close_panel()

        for mode in ("manual", "safe", "reviewed", "high"):
            submit(f"/permissions autonomy {mode}", f"Command autonomy: {mode}")
            pending = submit("/bash git status", "Permission required")
            if "risk critical" not in pending or "Allow session" in pending:
                raise RuntimeError(f"{mode} weakened native CriticalAsk")
            send_keys(ctx.tmux, session, "e")
            time.sleep(0.2)
            if requests:
                raise RuntimeError(f"{mode} disclosed native CriticalAsk")
            send_keys(ctx.tmux, session, "d")
            wait_for(ctx.tmux, session, r"Command /bash", "human-only denial")
            close_panel()
        submit("/permissions review off", "Qwen review off")
        before = len(requests)
        submit("/bash git status", "Permission required")
        time.sleep(0.2)
        if len(requests) != before or "Recommendation:" in capture(ctx.tmux, session):
            raise RuntimeError("disabled review sent a request or stale advice reached next command")
        state["delay"] = 0
        send_keys(ctx.tmux, session, "e")
        time.sleep(0.2)
        on_demand = capture(ctx.tmux, session)
        if "Permission required" not in on_demand:
            raise RuntimeError("on-demand explanation approved a command")
        save_evidence(ctx.root, "review-on-demand", on_demand)
        send_keys(ctx.tmux, session, "d")
        wait_for(ctx.tmux, session, r"Command /bash", "git status denied")
        close_panel()
        if requests:
            raise RuntimeError(f"unexpected number of reviewer requests: {len(requests)}")
        _finish_main(ctx.tmux, session)
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=2)
