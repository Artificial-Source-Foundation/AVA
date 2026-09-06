#!/usr/bin/env python3
"""Focused lifecycle self-tests for the shared fake-provider owner and broker."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

from fake_provider import launch_fake_provider
from timeout_support import test_timeout


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def _post_request(port: str, body: str = "{}") -> socket.socket:
    connection = socket.create_connection(("127.0.0.1", int(port)), timeout=test_timeout(5))
    request = (
        "POST /v1/chat/completions HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        f"Content-Length: {len(body)}\r\n"
        "Connection: close\r\n\r\n"
        f"{body}"
    )
    connection.sendall(request.encode("ascii"))
    return connection


def _provider_pids(port_file: Path) -> list[int]:
    """Find live provider processes by their unique port-file command argument."""

    matches: list[int] = []
    needle = str(port_file)
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            cmdline = (entry / "cmdline").read_bytes()
        except OSError:
            continue
        if needle.encode() in cmdline.split(b"\0"):
            matches.append(int(entry.name))
    return matches


def owner_gate_blocks_until_release(provider_exe: Path, root: Path) -> None:
    provider = launch_fake_provider(provider_exe, root / "owner", scenario="text-delayed")
    try:
        connection = _post_request(provider.port)
        try:
            provider.wait_for_request(0, "owner request gate", test_timeout(5))
            connection.settimeout(0.3)
            try:
                premature = connection.recv(4096)
            except socket.timeout:
                premature = b""
            require(premature == b"", f"delayed provider responded before its gate release: {premature!r}")
            provider.release_request(0)
            connection.settimeout(test_timeout(5))
            response = b""
            while True:
                chunk = connection.recv(4096)
                if not chunk:
                    break
                response += chunk
            require(response.startswith(b"HTTP/1.1 200"), f"delayed provider response after release: {response!r}")
        finally:
            connection.close()
        provider.finish(test_timeout(5))
        require("--- request 1 ---" in provider.request_log.read_text(encoding="utf-8"), "owner request log was not recorded")
    finally:
        provider.stop()
        # Repeated stops and stops after finish must stay harmless.
        provider.stop()


def owner_startup_failure_cleans_up(provider_exe: Path, root: Path) -> None:
    missing = root / "no-such-provider"
    try:
        launch_fake_provider(missing, root / "missing")
    except OSError:
        pass
    else:
        raise RuntimeError("launching a missing provider executable unexpectedly succeeded")
    require(_provider_pids(root / "missing" / "provider.port") == [], "failed launch left a provider process behind")


def _write_provider_fixture(path: Path, body: str) -> None:
    path.write_text(f"#!{sys.executable}\n{body}", encoding="utf-8")
    path.chmod(0o700)


def _require_finished_owner_closed(provider, expected_status: int) -> None:
    require(provider.process.poll() == expected_status, f"provider was not reaped with status {expected_status}")
    require(provider._stdout.closed, "provider stdout descriptor remained open")
    require(provider._stderr.closed, "provider stderr descriptor remained open")
    require(provider.gates.fileno == -1, "provider process-gate descriptor remained open")


def owner_finish_failures_clean_up(root: Path) -> tuple[Path, Path]:
    natural_nonzero = root / "natural-nonzero.py"
    _write_provider_fixture(
        natural_nonzero,
        """from pathlib import Path
import sys
import time
Path(sys.argv[1]).write_text("12345\\n", encoding="utf-8")
time.sleep(0.2)
raise SystemExit(23)
""",
    )
    provider = launch_fake_provider(natural_nonzero, root / "owner-natural-nonzero")
    try:
        provider.finish(test_timeout(5))
    except RuntimeError as error:
        require("exited with 23" in str(error), f"natural nonzero exit was not reported: {error}")
    else:
        raise RuntimeError("natural nonzero provider exit unexpectedly passed finish")
    _require_finished_owner_closed(provider, 23)
    provider.stop()

    term_42 = root / "hang-until-term.py"
    _write_provider_fixture(
        term_42,
        """from pathlib import Path
import signal
import sys

def stop(_signum, _frame):
    raise SystemExit(42)

signal.signal(signal.SIGTERM, stop)
Path(sys.argv[1]).write_text("12345\\n", encoding="utf-8")
signal.pause()
""",
    )
    provider = launch_fake_provider(term_42, root / "owner-finish-timeout")
    try:
        provider.finish(0.1)
    except RuntimeError as error:
        require("did not finish" in str(error), f"finish timeout was not reported: {error}")
    else:
        raise RuntimeError("hanging provider unexpectedly passed finish")
    _require_finished_owner_closed(provider, 42)
    provider.stop()
    return natural_nonzero, term_42


class BrokerSession:
    def __init__(self, provider_py: Path, provider_exe: Path, directory: Path, *, scenario: str = "text") -> None:
        self.process = subprocess.Popen(
            [
                sys.executable,
                str(provider_py),
                "broker",
                "--provider",
                str(provider_exe),
                "--directory",
                str(directory),
                "--prefix",
                "provider",
                "--delay-ms",
                "0",
                "--scenario",
                scenario,
                "--target",
                "unused",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        assert self.process.stdout is not None
        ready = self.process.stdout.readline().strip()
        port_marker = "ready port="
        if not ready.startswith(port_marker):
            stderr = self.process.stderr.read() if self.process.stderr else ""
            raise RuntimeError(f"broker did not become ready: {ready!r} stderr:\n{stderr}")
        self.port = ready[len(port_marker) :]

    def command(self, line: str) -> str:
        assert self.process.stdin is not None and self.process.stdout is not None
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()
        deadline = time.monotonic() + test_timeout(30)
        while time.monotonic() < deadline:
            reply = self.process.stdout.readline()
            if reply:
                return reply.strip()
            if self.process.poll() is not None:
                raise RuntimeError(f"broker exited during {line!r}: {self.process.returncode}")
        raise RuntimeError(f"broker did not reply to {line!r}")

    def close_stdin(self) -> None:
        assert self.process.stdin is not None
        self.process.stdin.close()


def broker_protocol_and_checked_finish(provider_py: Path, provider_exe: Path, root: Path) -> None:
    directory = root / "broker-protocol"
    broker = BrokerSession(provider_py, provider_exe, directory, scenario="text-delayed")
    connection = _post_request(broker.port)
    try:
        require(broker.command("wait 0 5") == "ok", "broker wait did not observe request 0")
        connection.settimeout(0.3)
        try:
            premature = connection.recv(4096)
        except socket.timeout:
            premature = b""
        require(premature == b"", "broker-owned provider responded before release")
        require(broker.command("release 0") == b"ok".decode(), "broker release failed")
        connection.settimeout(test_timeout(5))
        require(connection.recv(4096).startswith(b"HTTP/1.1 200"), "broker-owned provider did not respond after release")
    finally:
        connection.close()
    require(broker.command("wait 9 0.2").startswith("error"), "broker wait for an unserved request did not fail")
    require(broker.command("finish 3601").startswith("error"), "broker accepted an unbounded finish timeout")
    require(broker.command("nonsense").startswith("error"), "broker accepted an unknown command")
    require(broker.command("finish 5") == "ok", "broker checked finish failed")
    require(broker.process.wait(timeout=test_timeout(5)) == 0, "broker did not exit 0 after a checked finish")
    require(_provider_pids(directory / "provider.port") == [], "broker left a provider process behind")


def broker_finish_failures_exit_nonzero(provider_py: Path, natural_nonzero: Path, term_42: Path, root: Path) -> None:
    for name, executable, timeout, diagnostic in (
        ("natural-nonzero", natural_nonzero, 5, "exited with 23"),
        ("finish-timeout", term_42, 0.1, "did not finish"),
    ):
        directory = root / f"broker-{name}"
        broker = BrokerSession(provider_py, executable, directory)
        reply = broker.command(f"finish {timeout}")
        require(reply.startswith("error"), f"broker finish failure was not rejected: {reply!r}")
        require(diagnostic in reply, f"broker finish failure omitted its diagnostic: {reply!r}")
        require(broker.process.wait(timeout=test_timeout(5)) != 0, "broker exited 0 after provider finish failure")
        require(_provider_pids(directory / "provider.port") == [], "failed broker finish left a provider process behind")


def broker_eof_owns_provider_cleanup(provider_py: Path, provider_exe: Path, root: Path) -> None:
    directory = root / "broker-eof"
    broker = BrokerSession(provider_py, provider_exe, directory)
    pids = _provider_pids(directory / "provider.port")
    require(len(pids) == 1, f"expected exactly one owned provider process, saw {pids}")
    broker.close_stdin()
    require(broker.process.wait(timeout=test_timeout(5)) == 0, "broker did not exit 0 on stdin EOF")
    deadline = time.monotonic() + test_timeout(5)
    while time.monotonic() < deadline and _provider_pids(directory / "provider.port"):
        time.sleep(0.02)
    require(_provider_pids(directory / "provider.port") == [], "broker EOF did not reap the provider process")


def broker_startup_failure_is_reported(provider_py: Path, provider_exe: Path, root: Path) -> None:
    process = subprocess.Popen(
        [
            sys.executable,
            str(provider_py),
            "broker",
            "--provider",
            str(root / "no-such-provider"),
            "--directory",
            str(root / "broker-missing"),
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    assert process.stdout is not None
    first_line = process.stdout.readline().strip()
    require(first_line.startswith("error"), f"missing provider was not reported as a broker error: {first_line!r}")
    require(process.wait(timeout=test_timeout(5)) != 0, "broker exited 0 despite a missing provider")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--provider", required=True, type=Path)
    args = parser.parse_args()
    provider_exe = args.provider.absolute()
    require(provider_exe.is_file(), f"fake provider executable does not exist: {provider_exe}")
    provider_py = Path(__file__).with_name("fake_provider.py")
    with tempfile.TemporaryDirectory(prefix="ava-fake-provider-test-") as temporary:
        root = Path(temporary)
        owner_gate_blocks_until_release(provider_exe, root)
        owner_startup_failure_cleans_up(provider_exe, root)
        natural_nonzero, term_42 = owner_finish_failures_clean_up(root)
        broker_protocol_and_checked_finish(provider_py, provider_exe, root)
        broker_finish_failures_exit_nonzero(provider_py, natural_nonzero, term_42, root)
        broker_eof_owns_provider_cleanup(provider_py, provider_exe, root)
        broker_startup_failure_is_reported(provider_py, provider_exe, root)
    print("shared fake-provider owner and broker lifecycle checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
