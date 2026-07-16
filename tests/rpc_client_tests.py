#!/usr/bin/env python3
"""Deterministic unit/integration tests for the standard-library RPC client."""

from __future__ import annotations

import argparse
import json
import pathlib
import queue
import sys
import threading
from typing import Any


def fake_command(source: str) -> list[str]:
    return [sys.executable, "-u", "-c", source]


def test_timeout_tombstone(AvaRpcClient: type[Any]) -> None:
    server = r'''
import json, sys, time
first = json.loads(sys.stdin.readline())
time.sleep(0.12)
print(json.dumps({"id": first["id"], "type": "response", "success": True, "result": {"generation": 1}}), flush=True)
print(json.dumps({"schema_version": 1, "event_id": "late", "name": "late_response_sent", "type": "late_response_sent", "payload": {}}), flush=True)
second = json.loads(sys.stdin.readline())
print(json.dumps({"id": second["id"], "type": "response", "success": True, "result": {"generation": 2}}), flush=True)
'''
    client = AvaRpcClient(fake_command(server))
    try:
        try:
            client.request("get_state", request_id="reused", timeout=0.02)
            raise AssertionError("timed request unexpectedly completed")
        except queue.Empty:
            pass
        try:
            client.request("get_state", request_id="reused", timeout=0.2)
            raise AssertionError("timed-out id was reusable before its late response")
        except ValueError as error:
            assert "already pending" in str(error)
        event = client.events.get(timeout=2)
        assert event["name"] == "late_response_sent"
        assert client.request("get_state", request_id="reused", timeout=2) == {"generation": 2}
        assert client.close(timeout=2) == 0
    finally:
        if client.process.poll() is None:
            client.process.kill()
            client.process.wait()


def test_resolver_cancel_race_isolated(AvaRpcClient: type[Any], RpcError: type[BaseException]) -> None:
    server = r'''
import json, sys
prompt = json.loads(sys.stdin.readline())
print(json.dumps({
  "schema_version": 1, "event_id": "permission-event", "request_id": prompt["id"],
  "correlation_id": prompt["id"], "name": "permission_requested", "type": "permission_requested",
  "payload": {"resolver_request_id": "permission-race", "operation": "edit"}
}), flush=True)
other = None
late = False
while True:
    line = sys.stdin.readline()
    if not line:
        break
    request = json.loads(line)
    if request["type"] == "get_protocol":
        other = request["id"]
    elif request["type"] == "cancel":
        print(json.dumps({"id": request["id"], "type": "response", "success": True, "result": {"active_run": True}}), flush=True)
        print(json.dumps({"id": prompt["id"], "type": "response", "success": False,
                          "error": {"code": "canceled", "message": "agent loop canceled"}}), flush=True)
    elif request["type"] == "permission_reply":
        print(json.dumps({"id": request["id"], "type": "response", "success": False,
                          "error": {"code": "invalid_request", "message": "permission_reply has no matching pending request"}}), flush=True)
        late = True
    if other is not None and late:
        print(json.dumps({"id": other, "type": "response", "success": True, "result": {"protocol_version": 1}}), flush=True)
        other = None
'''
    hook_started = threading.Event()
    release_hook = threading.Event()

    def permission_hook(_event: dict[str, Any]) -> str:
        hook_started.set()
        assert release_hook.wait(2)
        return "allow"

    client = AvaRpcClient(fake_command(server), on_permission=permission_hook)
    prompt_result: list[BaseException | dict[str, Any]] = []
    other_result: list[BaseException | dict[str, Any]] = []

    def prompt_request() -> None:
        try:
            prompt_result.append(client.request("prompt", request_id="prompt", message="edit", timeout=2))
        except BaseException as error:
            prompt_result.append(error)

    def unrelated_request() -> None:
        try:
            other_result.append(client.request("get_protocol", request_id="other", timeout=2))
        except BaseException as error:
            other_result.append(error)

    prompt_thread = threading.Thread(target=prompt_request)
    other_thread = threading.Thread(target=unrelated_request)
    try:
        prompt_thread.start()
        assert hook_started.wait(2)
        other_thread.start()
        canceled = client.request("cancel", request_id="cancel", timeout=2)
        assert canceled["active_run"] is True
        release_hook.set()
        prompt_thread.join(2)
        other_thread.join(2)
        assert not prompt_thread.is_alive() and not other_thread.is_alive()
        assert prompt_result and isinstance(prompt_result[0], RpcError)
        assert other_result == [{"protocol_version": 1}]
        try:
            client.hook_errors.get_nowait()
            raise AssertionError("benign late resolver rejection was reported as a hook error")
        except queue.Empty:
            pass
        assert client.close(timeout=2) == 0
    finally:
        release_hook.set()
        prompt_thread.join(2)
        other_thread.join(2)
        if client.process.poll() is None:
            client.process.kill()
            client.process.wait()


def test_hook_failure_queue_does_not_poison_request(AvaRpcClient: type[Any]) -> None:
    server = r'''
import json, sys
prompt = json.loads(sys.stdin.readline())
print(json.dumps({"schema_version": 1, "event_id": "question", "request_id": prompt["id"],
                  "correlation_id": prompt["id"], "name": "question_requested", "type": "question_requested",
                  "payload": {"resolver_request_id": "question-1"}}), flush=True)
print(json.dumps({"id": prompt["id"], "type": "response", "success": True, "result": {"final_text": "still completed"}}), flush=True)
'''
    callback_errors: list[BaseException] = []

    def broken_hook(_event: dict[str, Any]) -> None:
        raise ValueError("hook failed independently")

    client = AvaRpcClient(fake_command(server), on_question=broken_hook, on_hook_error=callback_errors.append)
    try:
        assert client.request("prompt", request_id="prompt", message="ask", timeout=2)["final_text"] == "still completed"
        error = client.hook_errors.get(timeout=2)
        assert isinstance(error, ValueError) and callback_errors == [error]
        assert client.close(timeout=2) == 0
    finally:
        if client.process.poll() is None:
            client.process.kill()
            client.process.wait()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    args = parser.parse_args()
    sys.path.insert(0, str(args.source / "examples/rpc-client"))
    from ava_rpc_client import AvaRpcClient, RpcError  # pylint: disable=import-error,import-outside-toplevel

    test_timeout_tombstone(AvaRpcClient)
    test_resolver_cancel_race_isolated(AvaRpcClient, RpcError)
    test_hook_failure_queue_does_not_poison_request(AvaRpcClient)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
