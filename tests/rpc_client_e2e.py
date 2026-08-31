#!/usr/bin/env python3
"""Real AVA subprocess proof for examples/rpc-client."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from typing import Any

from process_gate import PROCESS_GATE_FD_ENV, ProcessGateSet, create_process_gate_pair
from timeout_support import test_timeout


def wait_for(predicate, timeout: float, message: str) -> None:
    deadline = time.monotonic() + test_timeout(timeout)
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise AssertionError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ava", required=True, type=pathlib.Path)
    parser.add_argument("--fake-provider", required=True, type=pathlib.Path)
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--root", required=True, type=pathlib.Path)
    args = parser.parse_args()
    sys.path.insert(0, str(args.source / "examples/rpc-client"))
    from ava_rpc_client import AvaRpcClient, RpcError  # pylint: disable=import-error,import-outside-toplevel

    root = args.root.absolute()
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    clients: list[AvaRpcClient] = []
    providers: list[subprocess.Popen[bytes]] = []
    provider_gates: dict[subprocess.Popen[bytes], ProcessGateSet] = {}
    worker_threads: list[threading.Thread] = []

    def base_environment(case: pathlib.Path) -> dict[str, str]:
        env = os.environ.copy()
        env.update(
            {
                "HOME": str(case / "home"),
                "XDG_CONFIG_HOME": str(case / "config"),
                "XDG_STATE_HOME": str(case / "state"),
                "XDG_DATA_HOME": str(case / "data"),
                "NO_COLOR": "1",
            }
        )
        for directory in ("home", "config/ava", "state", "data", "workspace"):
            (case / directory).mkdir(parents=True, exist_ok=True)
        (case / "config/ava").chmod(0o700)
        return env

    def start_provider(case: pathlib.Path, scenario: str, target: str = "", delay_ms: int = 0) -> tuple[subprocess.Popen[bytes], dict[str, str]]:
        env = base_environment(case)
        port_file = case / "provider.port"
        request_log = case / "provider.requests"
        command = [str(args.fake_provider), str(port_file), str(request_log), str(delay_ms)]
        if scenario != "text" or target:
            command += [scenario, target]
        gate_pair = create_process_gate_pair()
        provider_environment = os.environ.copy()
        provider_environment[PROCESS_GATE_FD_ENV] = str(gate_pair.child_fd)
        try:
            provider = subprocess.Popen(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=provider_environment,
                pass_fds=(gate_pair.child_fd,),
            )
        except BaseException:
            gate_pair.close()
            raise
        gate_pair.close_child_endpoint()
        providers.append(provider)
        provider_gates[provider] = gate_pair.gates
        try:
            wait_for(lambda: port_file.exists() and port_file.stat().st_size > 0, 5, "fake provider did not publish port")
            port = port_file.read_text(encoding="ascii").strip()
        except BaseException:
            cleanup_process(provider)
            raise
        env["MOONSHOT_API_KEY"] = "rpc-client-test-key"
        env["MOONSHOT_BASE_URL"] = f"http://127.0.0.1:{port}"
        model = {
            "default_provider": "moonshot",
            "default_model": "rpc-client-fake",
            "models": [
                {
                    "provider": "moonshot",
                    "id": "rpc-client-fake",
                    "family": "fake",
                    "context_window_tokens": 8192,
                    "max_output_tokens": 1024,
                    "supports_tools": True,
                    "supports_streaming": scenario == "rpc-stream",
                    "supports_reasoning": False,
                    "reports_usage": True,
                }
            ],
        }
        (case / "config/ava/models.json").write_text(json.dumps(model), encoding="utf-8")
        return provider, env

    def cleanup_process(process: subprocess.Popen[Any], timeout: float = 3) -> None:
        if process.poll() is not None:
            return
        process.terminate()
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=timeout)

    def track_client(client: AvaRpcClient) -> AvaRpcClient:
        clients.append(client)
        return client

    def finish_provider(provider: subprocess.Popen[bytes], timeout: float = 8) -> None:
        try:
            status = provider.wait(timeout=test_timeout(timeout))
        except subprocess.TimeoutExpired:
            cleanup_process(provider)
            raise AssertionError("fake provider did not finish")
        if status != 0:
            stderr = provider.stderr.read().decode("utf-8", errors="replace") if provider.stderr else ""
            raise AssertionError(f"fake provider failed ({status}): {stderr}")
        provider_gates.pop(provider).close()

    try:
        # Local state, malformed recovery, session operation, protocol discovery, clean EOF.
        case = root / "local"
        env = base_environment(case)
        client = track_client(AvaRpcClient([str(args.ava), "--rpc", "--offline"], cwd=str(case / "workspace"), env=env))
        protocol = client.request("get_protocol", request_id="protocol", timeout=test_timeout(5))
        assert protocol["protocol_version"] == 1 and protocol["event_schema_version"] == 1
        assert client.process.stdin is not None
        client.process.stdin.write(b"not-json\n")
        client.process.stdin.flush()
        initial = client.request("get_state", request_id="state", timeout=test_timeout(5))
        created = client.request("new_session", request_id="new", timeout=test_timeout(5))
        assert initial["session_id"] != created["session_id"] and created["created"] is True
        assert client.close(timeout=test_timeout(5)) == 0

        # Real streaming provider turn and event correlation.
        case = root / "stream"
        provider, env = start_provider(case, "rpc-stream")
        client = track_client(AvaRpcClient([str(args.ava), "--rpc"], cwd=str(case / "workspace"), env=env))
        result = client.request("prompt", request_id="stream-prompt", message="stream please", timeout=test_timeout(8))
        names = [event["name"] for event in client.events_by_request["stream-prompt"]]
        assert result["final_text"] == "rpc stream" and "message_update" in names and "done" in names
        assert client.close(timeout=test_timeout(5)) == 0
        finish_provider(provider)

        # Tool execution plus permission hook/reply.
        case = root / "permission"
        target = case / "outside.txt"
        provider, env = start_provider(case, "write-tool", str(target))
        permission_events: list[dict[str, Any]] = []

        def allow_permission(event: dict[str, Any]) -> tuple[str, str]:
            permission_events.append(event)
            return "allow", "approved by Python RPC client test"

        client = track_client(
            AvaRpcClient([str(args.ava), "--rpc"], cwd=str(case / "workspace"), env=env, on_permission=allow_permission)
        )
        result = client.request("prompt", request_id="tool-prompt", message="write target", timeout=test_timeout(8))
        assert result["final_text"] == "after permission deny" and target.read_text(encoding="utf-8") == "rpc new\n"
        assert permission_events and permission_events[0]["payload"]["operation"] == "edit"
        try:
            client.reply_permission(permission_events[0], "allow", timeout=test_timeout(5))
            raise AssertionError("duplicate resolver reply unexpectedly succeeded")
        except RpcError as error:
            assert error.error["code"] == "invalid_request" and "no matching pending request" in error.error["message"]
        assert client.close(timeout=test_timeout(5)) == 0
        finish_provider(provider)

        # Structured question hook/reply.
        case = root / "question"
        provider, env = start_provider(case, "question-tool")
        question_events: list[dict[str, Any]] = []

        def answer_question(event: dict[str, Any]) -> dict[str, str]:
            question_events.append(event)
            return {"selected": "yes"}

        client = track_client(
            AvaRpcClient([str(args.ava), "--rpc"], cwd=str(case / "workspace"), env=env, on_question=answer_question)
        )
        result = client.request("prompt", request_id="question-prompt", message="ask", timeout=test_timeout(8))
        assert result["final_text"] == "after question reply" and question_events
        assert client.close(timeout=test_timeout(5)) == 0
        finish_provider(provider)

        # Cooperative cancellation while the real provider subprocess is delayed.
        case = root / "cancel"
        provider, env = start_provider(case, "text-delayed")
        client = track_client(AvaRpcClient([str(args.ava), "--rpc"], cwd=str(case / "workspace"), env=env))
        prompt_outcome: list[BaseException | dict[str, Any]] = []

        def run_prompt() -> None:
            try:
                prompt_outcome.append(client.request("prompt", request_id="cancel-prompt", message="wait", timeout=test_timeout(8)))
            except BaseException as error:  # expected canceled RpcError
                prompt_outcome.append(error)

        prompt_thread = threading.Thread(target=run_prompt)
        worker_threads.append(prompt_thread)
        prompt_thread.start()
        provider_gates[provider].wait(0, test_timeout(5))
        canceled = client.request("cancel", request_id="cancel", timeout=test_timeout(5))
        prompt_thread.join(timeout=test_timeout(8))
        assert not prompt_thread.is_alive() and canceled["active_run"] is True
        assert prompt_outcome and isinstance(prompt_outcome[0], RpcError)
        prompt_error = prompt_outcome[0].error
        assert prompt_error["category"] == "unknown" and prompt_error["code"] == "canceled"
        assert prompt_error["message"] == "agent loop canceled"
        assert prompt_error["details"] == "unknown: agent loop canceled\n  boundary: during_provider_request"
        cancel_events = client.events_by_request["cancel-prompt"]
        assert any(event["name"] == "canceled" for event in cancel_events)
        assert all(event["name"] != "done" for event in cancel_events)
        provider_gates[provider].open(0)
        assert client.close(timeout=test_timeout(5)) == 0
        finish_provider(provider)

        # Compaction is a joinable worker: stdin remains live and cancellation
        # completes before the delayed provider response can arrive.
        case = root / "compact-cancel"
        provider, env = start_provider(case, "compact-delayed")
        client = track_client(AvaRpcClient([str(args.ava), "--rpc"], cwd=str(case / "workspace"), env=env))
        seeded = client.request("prompt", request_id="compact-seed", message="seed compaction", timeout=test_timeout(5))
        assert seeded["final_text"] == "before compact"
        compact_outcome: list[BaseException | dict[str, Any]] = []

        def run_compact() -> None:
            try:
                compact_outcome.append(client.request("compact", request_id="compact-cancel", instructions="summarize", timeout=test_timeout(8)))
            except BaseException as error:
                compact_outcome.append(error)

        compact_thread = threading.Thread(target=run_compact)
        worker_threads.append(compact_thread)
        compact_thread.start()
        provider_gates[provider].wait(1, test_timeout(5))
        cancel_started = time.monotonic()
        canceled = client.request("cancel", request_id="compact-cancel-request", timeout=test_timeout(2))
        compact_thread.join(timeout=1.2)
        cancel_elapsed = time.monotonic() - cancel_started
        assert canceled["active_run"] is True
        assert not compact_thread.is_alive() and cancel_elapsed < 1.2
        assert len(compact_outcome) == 1 and isinstance(compact_outcome[0], RpcError)
        assert compact_outcome[0].error["code"] == "canceled"
        provider_gates[provider].open(1)
        assert client.close(timeout=test_timeout(5)) == 0
        cleanup_process(provider)
        return 0
    finally:
        for client in reversed(clients):
            if client.process.poll() is None:
                client.close_stdin()
                try:
                    client.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    cleanup_process(client.process)
        for provider in reversed(providers):
            cleanup_process(provider)
            gates = provider_gates.pop(provider, None)
            if gates is not None:
                gates.close()
        for thread in worker_threads:
            thread.join(timeout=3)


if __name__ == "__main__":
    raise SystemExit(main())
