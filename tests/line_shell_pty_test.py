#!/usr/bin/env python3
"""PTY proof for the explicit line-oriented frontend and default TUI selection."""

from __future__ import annotations

import argparse
import errno
import fcntl
import json
import os
import pathlib
import pty
import select
import shutil
import signal
import struct
import subprocess
import sys
import termios
import time

from fake_provider import launch_fake_provider


MAX_CAPTURE_BYTES = 2 * 1024 * 1024
DEADLINE_SECONDS = 15.0
ENVIRONMENT_ALLOWLIST = (
    "PATH",
    "LANG",
    "LC_ALL",
    "LC_CTYPE",
    "LD_LIBRARY_PATH",
    "DYLD_LIBRARY_PATH",
    "DYLD_FALLBACK_LIBRARY_PATH",
    "ASAN_OPTIONS",
    "UBSAN_OPTIONS",
    "LSAN_OPTIONS",
    "TSAN_OPTIONS",
    "MSAN_OPTIONS",
    "ASAN_SYMBOLIZER_PATH",
    "LLVM_SYMBOLIZER_PATH",
    "TMPDIR",
    "TZ",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def process_group_exists(pgid: int) -> bool:
    try:
        os.killpg(pgid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        # Darwin reports EPERM for a zombie-only process group. Every group
        # probed here was created by this test, so no live foreign process can
        # legitimately cause the error.
        return sys.platform != "darwin"


def terminate_group(process: subprocess.Popen[bytes], timeout: float = 2.0) -> None:
    pgid = process.pid
    # Reap an exited leader before probing its group. Darwin reports EPERM for
    # killpg against a zombie-only process group instead of ESRCH.
    process.poll()
    if process_group_exists(pgid):
        try:
            os.killpg(pgid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            pass
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and process_group_exists(pgid):
        process.poll()
        select.select([], [], [], 0.05)
    if process_group_exists(pgid):
        try:
            os.killpg(pgid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        pass


def set_winsize(fd: int, rows: int = 30, columns: int = 110) -> None:
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))


class PtyAva:
    def __init__(self, ava: pathlib.Path, arguments: list[str], workspace: pathlib.Path, environment: dict[str, str]) -> None:
        self.master_fd, slave_fd = pty.openpty()
        set_winsize(slave_fd)
        self.process = subprocess.Popen(
            [str(ava), *arguments],
            cwd=workspace,
            env=environment,
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            start_new_session=True,
        )
        os.close(slave_fd)
        self.capture = bytearray()

    def read_until(self, expected: bytes, label: str, timeout: float = DEADLINE_SECONDS) -> bytes:
        return self.read_until_count(expected, 1, label, timeout)

    def read_until_count(self, expected: bytes, count: int, label: str, timeout: float = DEADLINE_SECONDS) -> bytes:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.capture.count(expected) >= count:
                return bytes(self.capture)
            ready, _, _ = select.select([self.master_fd], [], [], min(0.1, max(0.0, deadline - time.monotonic())))
            if ready:
                try:
                    chunk = os.read(self.master_fd, 16384)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    chunk = b""
                if chunk:
                    self.capture.extend(chunk)
                    if len(self.capture) > MAX_CAPTURE_BYTES:
                        raise RuntimeError(f"PTY capture exceeded {MAX_CAPTURE_BYTES} bytes while waiting for {label}")
                    continue
            if self.process.poll() is not None:
                raise RuntimeError(f"AVA exited before {label}: rc={self.process.returncode} capture={bytes(self.capture)!r}")
        raise RuntimeError(f"timed out waiting for {label}; expected={expected!r} capture={bytes(self.capture)!r}")

    def send(self, data: bytes) -> None:
        os.write(self.master_fd, data)

    def wait(self, timeout: float = DEADLINE_SECONDS) -> tuple[int, bytes]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.master_fd], [], [], 0.1)
            if ready:
                try:
                    chunk = os.read(self.master_fd, 16384)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    chunk = b""
                if chunk:
                    self.capture.extend(chunk)
                    require(len(self.capture) <= MAX_CAPTURE_BYTES, "PTY exit capture exceeded its byte limit")
            returncode = self.process.poll()
            if returncode is not None:
                return returncode, bytes(self.capture)
        raise RuntimeError(f"AVA did not exit within {timeout:.1f}s; capture={bytes(self.capture)!r}")

    def close(self) -> None:
        terminate_group(self.process)
        os.close(self.master_fd)


class FakeProvider:
    """Thin adapter keeping this harness's constructor shape on the shared owner."""

    def __init__(self, executable: pathlib.Path, root: pathlib.Path, name: str, scenario: str, target: pathlib.Path | None, environment: dict[str, str]) -> None:
        self._provider = launch_fake_provider(
            executable,
            root,
            prefix=name,
            delay_ms=0,
            scenario=scenario,
            target=target or "",
            environment=environment,
        )
        self.port_file = self._provider.port_file
        self.port = self._provider.port

    def close(self) -> None:
        self._provider.stop()


def isolated_environment(root: pathlib.Path) -> dict[str, str]:
    environment = {name: os.environ[name] for name in ENVIRONMENT_ALLOWLIST if name in os.environ}
    directories = {
        "HOME": root / "home",
        "XDG_CONFIG_HOME": root / "config",
        "XDG_STATE_HOME": root / "state",
        "XDG_DATA_HOME": root / "data",
        "XDG_CACHE_HOME": root / "cache",
        "XDG_RUNTIME_DIR": root / "runtime",
    }
    for name, directory in directories.items():
        directory.mkdir(parents=True, exist_ok=True)
        environment[name] = str(directory)
    directories["XDG_RUNTIME_DIR"].chmod(0o700)
    # Debug builds must not write libcwd NOTICE/startup output into the PTY
    # protocol this harness parses; keep the child quiet like other real-process
    # tests, while still allowing explicit per-test debug routing overrides.
    environment.update(
        {
            "TERM": "xterm-256color",
            "NO_COLOR": "1",
            "AVA_SESSION_TITLES": "off",
            "LIBCWD_NO_STARTUP_MSGS": "1",
            "AVA_NO_DEBUG_OUTPUT": "1",
        }
    )
    for name in ("AVA_TEST_NAME", "AVA_DEBUG_OUTPUT_DIR"):
        value = os.environ.get(name)
        if value is not None:
            environment[name] = value
    return environment


def write_fake_model_config(root: pathlib.Path) -> None:
    model_config = {
        "default_provider": "moonshot",
        "default_model": "ava-line-shell-fake",
        "models": [
            {
                "provider": "moonshot",
                "id": "ava-line-shell-fake",
                "name": "AVA Line Shell Fake",
                "family": "fake",
                "context_window_tokens": 8192,
                "max_output_tokens": 1024,
                "supports_tools": True,
                "supports_streaming": False,
                "supports_reasoning": False,
                "reports_usage": True,
            }
        ],
    }
    config_dir = root / "config" / "ava"
    config_dir.mkdir(parents=True, exist_ok=True)
    (config_dir / "models.json").write_text(json.dumps(model_config) + "\n", encoding="utf-8")


def provider_environment(base: dict[str, str], provider: FakeProvider) -> dict[str, str]:
    environment = base.copy()
    environment.update({"MOONSHOT_API_KEY": "test-key", "MOONSHOT_BASE_URL": f"http://127.0.0.1:{provider.port}"})
    return environment


def assert_line_shell_protocol_free(capture: bytes, label: str) -> None:
    require(b"\x1b" not in capture, f"{label} emitted a terminal escape/protocol sequence: {capture!r}")
    require(b"\x9b" not in capture and b"\x9d" not in capture, f"{label} emitted a C1 terminal protocol byte: {capture!r}")


def run_hostile_output_and_eof(ava: pathlib.Path, fake_provider: pathlib.Path, root: pathlib.Path, workspace: pathlib.Path, base_env: dict[str, str]) -> None:
    provider = FakeProvider(fake_provider, root, "hostile", "terminal-hostile-text", None, base_env)
    process = PtyAva(ava, ["--line-shell", "--no-session"], workspace, provider_environment(base_env, provider))
    try:
        startup = process.read_until(b"[build] ava> ", "line-shell startup")
        require(b"line shell" in startup and b"Type a message or /help" in startup, f"line-shell startup was not concise and explicit: {startup!r}")
        assert_line_shell_protocol_free(startup, "line-shell startup")
        process.send(b"show hostile output\n")
        hostile = process.read_until(b"safe?]8;;https://example.invalid?link?]8;;? output", "sanitized hostile model output")
        assert_line_shell_protocol_free(hostile, "line-shell model output")
        process.read_until_count(b"[build] ava> ", 2, "line-shell prompt after model output")
        process.send(b"\x04")
        returncode, capture = process.wait()
        require(returncode == 0, f"line shell EOF exited with {returncode}: {capture!r}")
        require(b"Session history was not saved" in capture and b"Ready when you are" not in capture, f"line-shell EOF text was not concise: {capture!r}")
        assert_line_shell_protocol_free(capture, "complete line-shell session")
    finally:
        process.close()
        provider.close()


def run_permission_flows(ava: pathlib.Path, fake_provider: pathlib.Path, root: pathlib.Path, workspace: pathlib.Path, base_env: dict[str, str]) -> None:
    outside = root / "outside.txt"
    outside.write_text("outside fixture\n", encoding="utf-8")
    provider = FakeProvider(fake_provider, root, "permission", "read-tool-thrice", outside, base_env)
    process = PtyAva(ava, ["--line-shell", "--no-session"], workspace, provider_environment(base_env, provider))
    try:
        process.read_until(b"[build] ava> ", "permission session startup")
        process.send(b"first permission\n")
        process.read_until_count(b"Permission choice> ", 1, "permission allow prompt")
        process.send(b"invalid\nallow\n")
        process.read_until(b"first controlled grant", "permission allow completion")
        process.read_until_count(b"[build] ava> ", 2, "prompt after permission allow")

        process.send(b"second permission\n")
        process.read_until_count(b"Permission choice> ", 2, "permission deny prompt")
        process.send(b"deny\n")
        process.read_until(b"second controlled grant", "permission denial completion")
        process.read_until_count(b"[build] ava> ", 3, "prompt after permission denial")

        process.send(b"third permission\n")
        process.read_until_count(b"Permission choice> ", 3, "permission cancel prompt")
        process.send(b"cancel\n")
        process.read_until(b"request was not allowed", "permission cancellation explanation")
        process.read_until_count(b"[build] ava> ", 4, "prompt after permission cancellation")
        process.send(b"/exit\n")
        returncode, capture = process.wait()
        require(returncode == 0, f"permission line-shell session failed: rc={returncode} capture={capture!r}")
        require(
            b"Invalid permission answer" in capture
            and capture.count(b"Permission required") >= 3
            and b"Permission canceled" in capture,
            f"permission success/denial/cancel coverage was incomplete: {capture!r}",
        )
        assert_line_shell_protocol_free(capture, "permission line-shell session")
    finally:
        process.close()
        provider.close()


def run_question_flows(ava: pathlib.Path, fake_provider: pathlib.Path, root: pathlib.Path, workspace: pathlib.Path, base_env: dict[str, str]) -> None:
    provider = FakeProvider(fake_provider, root, "question-success", "question-tool-multi", None, base_env)
    process = PtyAva(ava, ["--line-shell", "--no-session"], workspace, provider_environment(base_env, provider))
    try:
        process.read_until(b"[build] ava> ", "question success startup")
        process.send(b"ask multi question\n")
        process.read_until(b"Answer> ", "multi-question prompt")
        process.send(b"1,1\n1,3\n")
        process.read_until(b"after multi question reply", "multi-question success")
        process.read_until_count(b"[build] ava> ", 2, "prompt after multi-question success")
        process.send(b"/exit\n")
        returncode, capture = process.wait()
        require(returncode == 0 and b"unique comma-separated numbers" in capture, f"question retry/success failed: {capture!r}")
        assert_line_shell_protocol_free(capture, "question success line-shell session")
    finally:
        process.close()
        provider.close()

    cancel_provider = FakeProvider(fake_provider, root, "question-cancel", "question-tool", None, base_env)
    cancel_process = PtyAva(ava, ["--line-shell", "--no-session"], workspace, provider_environment(base_env, cancel_provider))
    try:
        cancel_process.read_until(b"[build] ava> ", "question cancel startup")
        cancel_process.send(b"ask question then cancel\n")
        cancel_process.read_until(b"Answer> ", "question cancellation prompt")
        cancel_process.send(b"cancel\n")
        cancel_process.read_until(b"Question canceled", "question cancellation explanation")
        cancel_process.read_until(b"after question reply", "provider continuation after safe question cancellation")
        cancel_process.read_until_count(b"[build] ava> ", 2, "prompt after question cancellation")
        cancel_process.send(b"/exit\n")
        returncode, capture = cancel_process.wait()
        require(returncode == 0, f"question cancellation session failed: {capture!r}")
        assert_line_shell_protocol_free(capture, "question cancellation line-shell session")
    finally:
        cancel_process.close()
        cancel_provider.close()


def prove_default_tty_still_selects_tui(ava: pathlib.Path, workspace: pathlib.Path, environment: dict[str, str]) -> None:
    process = PtyAva(ava, ["--offline", "--no-session"], workspace, environment)
    try:
        capture = process.read_until(b"\x1b", "default TTY TUI terminal initialization", timeout=8.0)
        require(b"line shell" not in capture, f"default TTY unexpectedly selected the line shell: {capture!r}")
    finally:
        process.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ava", required=True)
    parser.add_argument("--fake-provider", required=True)
    parser.add_argument("--root", required=True)
    args = parser.parse_args()

    ava = pathlib.Path(args.ava).resolve()
    fake_provider = pathlib.Path(args.fake_provider).resolve()
    require(ava.is_file(), f"AVA executable does not exist: {ava}")
    require(fake_provider.is_file(), f"fake provider executable does not exist: {fake_provider}")
    root = pathlib.Path(args.root).resolve()
    if root.exists():
        require(root.name == "line-shell-pty", f"refusing to clear unexpected PTY root: {root}")
        shutil.rmtree(root)
    root.mkdir(parents=True, mode=0o700)
    workspace = root / "workspace"
    workspace.mkdir()
    (workspace / "AGENTS.md").write_text("line shell PTY fixture\n", encoding="utf-8")
    environment = isolated_environment(root)
    write_fake_model_config(root)

    run_hostile_output_and_eof(ava, fake_provider, root, workspace, environment)
    run_permission_flows(ava, fake_provider, root, workspace, environment)
    run_question_flows(ava, fake_provider, root, workspace, environment)
    prove_default_tty_still_selects_tui(ava, workspace, environment)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
