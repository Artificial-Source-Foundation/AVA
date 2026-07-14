#!/usr/bin/env python3
"""Small standard-library AVA RPC v1 subprocess client.

The implementation follows docs/rpc-protocol.md. It intentionally contains no
Pi RPC, JSON-RPC, or ACP assumptions.
"""

from __future__ import annotations

import json
import queue
import subprocess
import threading
import uuid
from collections import defaultdict
from collections.abc import Callable, Mapping, Sequence
from typing import Any

JsonObject = dict[str, Any]
PermissionHook = Callable[[JsonObject], str | tuple[str, str] | None]
QuestionHook = Callable[[JsonObject], Mapping[str, Any] | None]
EventHook = Callable[[JsonObject], None]
HookErrorHook = Callable[[BaseException], None]


class RpcError(RuntimeError):
    def __init__(self, error: Mapping[str, Any]):
        self.error = dict(error)
        super().__init__(f"{self.error.get('code', 'unknown')}: {self.error.get('message', 'RPC error')}")


class AvaRpcClient:
    """Launch and correlate one AVA RPC subprocess.

    ``timeout=None`` is deliberate: AVA permission/question waits are settled by
    hooks, explicit replies, cancel, EOF, or process/write failure rather than an
    arbitrary short client timeout.
    """

    def __init__(
        self,
        command: Sequence[str] = ("ava", "--rpc"),
        *,
        cwd: str | None = None,
        env: Mapping[str, str] | None = None,
        on_event: EventHook | None = None,
        on_permission: PermissionHook | None = None,
        on_question: QuestionHook | None = None,
        on_hook_error: HookErrorHook | None = None,
    ) -> None:
        self.process = subprocess.Popen(
            list(command),
            cwd=cwd,
            env=dict(env) if env is not None else None,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )
        self.on_event = on_event
        self.on_permission = on_permission
        self.on_question = on_question
        self.on_hook_error = on_hook_error
        self.events: queue.Queue[JsonObject] = queue.Queue()
        self.hook_errors: queue.Queue[BaseException] = queue.Queue()
        self.events_by_request: dict[str, list[JsonObject]] = defaultdict(list)
        self.stderr = bytearray()
        self._pending: dict[str, queue.Queue[JsonObject | BaseException]] = {}
        self._timed_out_ids: set[str] = set()
        self._pending_lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._resolver_lock = threading.Lock()
        self._resolver_threads: set[threading.Thread] = set()
        self._closed = threading.Event()
        self._reader_error: BaseException | None = None
        self._reader = threading.Thread(target=self._read_stdout, name="ava-rpc-stdout", daemon=True)
        self._stderr_reader = threading.Thread(target=self._read_stderr, name="ava-rpc-stderr", daemon=True)
        self._reader.start()
        self._stderr_reader.start()

    def _new_id(self, prefix: str) -> str:
        return f"{prefix}-{uuid.uuid4().hex}"

    def send(self, message: Mapping[str, Any]) -> None:
        """Send one UTF-8 JSON object followed by exactly one LF."""
        if self.process.stdin is None or self._closed.is_set():
            raise RuntimeError("AVA RPC stdin is closed")
        data = json.dumps(dict(message), ensure_ascii=False, separators=(",", ":")).encode("utf-8") + b"\n"
        with self._write_lock:
            try:
                self.process.stdin.write(data)
                self.process.stdin.flush()
            except (BrokenPipeError, OSError):
                # The stdout reader owns connection-wide failure. A resolver
                # reply can lose a cancel/EOF race without invalidating other
                # responses that are already in flight on stdout.
                raise

    def _request_message(self, message: Mapping[str, Any], timeout: float | None) -> JsonObject:
        request_id = message.get("id")
        if not isinstance(request_id, str) or not request_id:
            raise ValueError("RPC request id must be a non-empty string")
        waiter: queue.Queue[JsonObject | BaseException] = queue.Queue(maxsize=1)
        with self._pending_lock:
            if request_id in self._pending or request_id in self._timed_out_ids:
                raise ValueError(f"request id is already pending: {request_id}")
            self._pending[request_id] = waiter
        try:
            self.send(message)
            item = waiter.get(timeout=timeout)
        except queue.Empty:
            with self._pending_lock:
                if self._pending.get(request_id) is waiter:
                    self._pending.pop(request_id, None)
                    self._timed_out_ids.add(request_id)
                    raise
            item = waiter.get_nowait()
        except BaseException:
            with self._pending_lock:
                if self._pending.get(request_id) is waiter:
                    self._pending.pop(request_id, None)
            raise
        if isinstance(item, BaseException):
            raise item
        if not item.get("success"):
            raise RpcError(item.get("error", {}))
        result = item.get("result", {})
        if not isinstance(result, dict):
            raise RuntimeError("AVA RPC success result is not an object")
        return result

    def request(self, request_type: str, *, request_id: str | None = None, timeout: float | None = None, **fields: Any) -> JsonObject:
        request_id = request_id or self._new_id("request")
        return self._request_message({"id": request_id, "type": request_type, "protocol_version": 1, **fields}, timeout)

    def reply_permission(
        self,
        event: Mapping[str, Any],
        decision: str,
        reason: str | None = None,
        *,
        timeout: float | None = None,
    ) -> JsonObject:
        """Resolve a permission event and return its correlated RPC response."""
        payload = event.get("payload", {})
        reply: JsonObject = {
            "id": self._new_id("permission-reply"),
            "type": "permission_reply",
            "protocol_version": 1,
            "request_id": payload["resolver_request_id"],
            "correlation_id": event["correlation_id"],
            "decision": decision,
        }
        if reason is not None:
            reply["reason"] = reason
        return self._request_message(reply, timeout)

    def reply_question(self, event: Mapping[str, Any], *, timeout: float | None = None, **answer: Any) -> JsonObject:
        """Resolve a question event and return its correlated RPC response."""
        payload = event.get("payload", {})
        return self._request_message(
            {
                "id": self._new_id("question-reply"),
                "type": "question_reply",
                "protocol_version": 1,
                "request_id": payload["resolver_request_id"],
                "correlation_id": event["correlation_id"],
                **answer,
            },
            timeout,
        )

    def _dispatch_event(self, event: JsonObject) -> None:
        self.events.put(event)
        request_id = event.get("request_id")
        if isinstance(request_id, str):
            self.events_by_request[request_id].append(event)
        if self.on_event is not None:
            try:
                self.on_event(event)
            except BaseException as error:
                self._report_hook_error(error)
        if event.get("name") == "permission_requested" and self.on_permission is not None:
            self._start_resolver(lambda: self._resolve_permission_event(event))
        if event.get("name") == "question_requested" and self.on_question is not None:
            self._start_resolver(lambda: self._resolve_question_event(event))

    def _start_resolver(self, resolve: Callable[[], None]) -> None:
        def run() -> None:
            try:
                resolve()
            except BaseException as error:
                if not self._is_benign_resolver_race(error):
                    self._report_hook_error(error)
            finally:
                with self._resolver_lock:
                    self._resolver_threads.discard(threading.current_thread())

        thread = threading.Thread(target=run, name="ava-rpc-resolver", daemon=True)
        with self._resolver_lock:
            self._resolver_threads.add(thread)
        thread.start()

    def _report_hook_error(self, error: BaseException) -> None:
        self.hook_errors.put(error)
        if self.on_hook_error is not None:
            try:
                self.on_hook_error(error)
            except BaseException as callback_error:
                self.hook_errors.put(callback_error)

    def _is_benign_resolver_race(self, error: BaseException) -> bool:
        if isinstance(error, RpcError):
            code = error.error.get("code")
            message = error.error.get("message", "")
            return code == "canceled" or (code == "invalid_request" and "no matching pending request" in str(message))
        if isinstance(error, (BrokenPipeError, EOFError)):
            return True
        connection_closed = self.process.poll() is not None or self.process.stdin is None or self.process.stdin.closed
        if isinstance(error, OSError):
            return connection_closed
        return connection_closed and isinstance(error, RuntimeError) and "closed" in str(error).lower()

    def _resolve_permission_event(self, event: JsonObject) -> None:
        assert self.on_permission is not None
        resolution = self.on_permission(event)
        if isinstance(resolution, str):
            self.reply_permission(event, resolution)
        elif resolution is not None:
            self.reply_permission(event, resolution[0], resolution[1])

    def _resolve_question_event(self, event: JsonObject) -> None:
        assert self.on_question is not None
        answer = self.on_question(event)
        if answer is not None:
            self.reply_question(event, **dict(answer))

    def _read_stdout(self) -> None:
        assert self.process.stdout is not None
        buffer = bytearray()
        try:
            while True:
                chunk = self.process.stdout.read(4096)
                if not chunk:
                    break
                buffer.extend(chunk)
                while True:
                    delimiter = buffer.find(b"\n")
                    if delimiter < 0:
                        break
                    record = bytes(buffer[:delimiter])
                    del buffer[: delimiter + 1]
                    if record.endswith(b"\r"):
                        record = record[:-1]
                    self._handle_record(record)
            if buffer:
                if buffer.endswith(b"\r"):
                    buffer = buffer[:-1]
                self._handle_record(bytes(buffer))
            if self.process.poll() not in (None, 0):
                raise RuntimeError(f"AVA RPC exited with {self.process.returncode}")
            self._fail_all(EOFError("AVA RPC stdout closed"))
        except BaseException as error:
            self._reader_error = error
            self._fail_all(error)

    def _handle_record(self, record: bytes) -> None:
        message = json.loads(record.decode("utf-8", errors="strict"))
        if not isinstance(message, dict):
            raise RuntimeError("AVA RPC stdout record is not an object")
        if message.get("type") == "response":
            response_id = message.get("id")
            with self._pending_lock:
                if isinstance(response_id, str) and response_id in self._timed_out_ids:
                    self._timed_out_ids.remove(response_id)
                    return
                waiter = self._pending.pop(response_id, None) if isinstance(response_id, str) else None
                if waiter is not None:
                    waiter.put(message)
            return
        self._dispatch_event(message)

    def _read_stderr(self) -> None:
        assert self.process.stderr is not None
        while True:
            chunk = self.process.stderr.read(4096)
            if not chunk:
                return
            self.stderr.extend(chunk)

    def _fail_all(self, error: BaseException) -> None:
        with self._pending_lock:
            pending = list(self._pending.values())
            self._pending.clear()
            self._timed_out_ids.clear()
        for waiter in pending:
            waiter.put(error)

    def close_stdin(self) -> None:
        if self.process.stdin is not None and not self.process.stdin.closed:
            self.process.stdin.close()

    def close(self, timeout: float | None = None) -> int:
        self.close_stdin()
        return_code = self.process.wait(timeout=timeout)
        self._reader.join(timeout=timeout)
        self._stderr_reader.join(timeout=timeout)
        with self._resolver_lock:
            resolver_threads = list(self._resolver_threads)
        for thread in resolver_threads:
            thread.join(timeout=timeout)
        self._closed.set()
        if self._reader_error is not None and not isinstance(self._reader_error, EOFError):
            raise self._reader_error
        return return_code

    def __enter__(self) -> "AvaRpcClient":
        return self

    def __exit__(self, exc_type: Any, exc: Any, traceback: Any) -> None:
        if self.process.poll() is None:
            try:
                self.close(timeout=10)
            except BaseException:
                self.process.kill()
                self.process.wait()
