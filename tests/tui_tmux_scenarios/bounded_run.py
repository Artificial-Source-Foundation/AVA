"""Ten-round pause and explicit continuation through the native TUI."""

import json
import re
import shlex

from tui_smoke_helpers import save_evidence, send_keys, send_literal, wait_for, wait_for_absent
from .common import _assert_normal_turn_request_count_stays, _finish_main, _request_log_entries


def scenario_bounded_run(ctx):
    session = ctx.session_name("bounded-run")
    target = ctx.restore_workspace / "bounded.txt"
    target.write_text("Completed read result retained across Continue.\n", encoding="utf-8")
    provider = ctx.start_fake_provider("bounded-run", delay_ms=0, scenario="bounded-ten-rounds", target=target)
    command = ctx.pane_command(home=ctx.restore_home, config=ctx.restore_config,
                               state=ctx.restore_state, data=ctx.restore_data,
                               extra={"MOONSHOT_API_KEY": "test-key", "MOONSHOT_BASE_URL": f"http://127.0.0.1:{provider.port}",
                                      "AVA_SESSION_TITLES": "off", "NO_COLOR": "1"})
    model_path = ctx.restore_config / "ava" / "models.json"
    models = json.loads(model_path.read_text())
    for model in models["models"]:
        model["context_window_tokens"] = 64000
        model["supports_tools"] = True
    model_path.write_text(json.dumps(models), encoding="utf-8")
    ctx.launch_ava(session, workspace=ctx.restore_workspace, command=command, width=150, height=32)
    wait_for(ctx.tmux, session, r"Type a message", "bounded-run startup")
    send_literal(ctx.tmux, session, "Inspect the bounded fixture")
    send_keys(ctx.tmux, session, "Enter")
    receipt = "Paused after 10 tool rounds. Work is incomplete. Continue to resume."
    wait_for(ctx.tmux, session, receipt, "ten-round incomplete receipt", timeout=20)
    settled = wait_for_absent(ctx.tmux, session, r"assistant is writing", "responding state settled")
    if not re.search(r"ctx ~10(?:\.\d+)?k \(1[56]\.\d+%\)", settled):
        raise RuntimeError("context meter did not use the last provider input plus the estimated new result")
    _assert_normal_turn_request_count_stays(provider.request_log, 10, "cap sends no extra final-answer request")
    save_evidence(ctx.root, "bounded-run-paused", settled)
    entries = []
    for path in ctx.restore_state.rglob("session_*.jsonl"):
        entries.extend(json.loads(line) for line in path.read_text().splitlines() if line.strip())
    stops = [entry for entry in entries if entry.get("type") == "run_stop"
             and entry.get("data", {}).get("classification") == "max_turn_requests"]
    if len(stops) != 1 or stops[0]["data"].get("round_count") != 10 or stops[0]["data"].get("status") != "paused":
        raise RuntimeError("pause receipt was not persisted with its typed stop reason")
    persisted = list(ctx.restore_state.rglob("session_*.jsonl"))
    if len(persisted) != 1:
        raise RuntimeError("expected one isolated bounded-run session")
    _finish_main(ctx.tmux, session)
    session = ctx.session_name("bounded-reopened")
    ctx.launch_ava(session, workspace=ctx.restore_workspace,
                   command=command + " --session " + shlex.quote(persisted[0].stem), width=150, height=32)
    restored = wait_for(ctx.tmux, session, receipt, "persisted runtime notice after reopen", timeout=15)
    if "max_turn_requests" not in restored:
        raise RuntimeError("restored notice lost its typed bounded-stop classification")
    save_evidence(ctx.root, "bounded-run-reopened", restored)
    send_literal(ctx.tmux, session, "Continue")
    send_keys(ctx.tmux, session, "Enter")
    resumed = wait_for(ctx.tmux, session, "Resumed using completed tool results", "continuation", timeout=15)
    _assert_normal_turn_request_count_stays(provider.request_log, 11, "Continue sends exactly one new request")
    request_log = provider.request_log.read_text()
    if "Completed read result retained across Continue." not in _request_log_entries(request_log)[-1]:
        raise RuntimeError("Continue omitted completed tool results")
    results = []
    for path in ctx.restore_state.rglob("session_*.jsonl"):
        results.extend(json.loads(line) for line in path.read_text().splitlines() if line.strip())
    if sum(entry.get("type") == "tool_result" for entry in results) != 10:
        raise RuntimeError("Continue replayed completed tool calls")
    save_evidence(ctx.root, "bounded-run-continued", resumed)
    _finish_main(ctx.tmux, session)
