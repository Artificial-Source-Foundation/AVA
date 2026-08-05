#!/usr/bin/env python3
"""Regression coverage for ava_tests and real-ava libcwd output routing."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import signal
import stat
import subprocess
import tempfile
import time


MARKER_PREFIX = "AVA libcwd routing marker: suite="
APP_MARKER_PREFIX = "AVA libcwd routing marker: test="
PROCESS_TIMEOUT = 12.0
TERM_GRACE = 0.5
KILL_GRACE = 1.0
ACTIVE_PROCESSES: set[subprocess.Popen[str]] = set()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def terminate(process: subprocess.Popen[str]) -> None:
    if process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=TERM_GRACE)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=KILL_GRACE)
    try:
        process.communicate(timeout=KILL_GRACE)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate(timeout=KILL_GRACE)
    ACTIVE_PROCESSES.discard(process)


def interrupt_handler(signal_number: int, _frame: object) -> None:
    raise InterruptedError(f"libcwd routing test interrupted by signal {signal_number}")


def start(ava: Path, arguments: list[str], env: dict[str, str], cwd: Path) -> subprocess.Popen[str]:
    process = subprocess.Popen(
        [str(ava), *arguments],
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    ACTIVE_PROCESSES.add(process)
    return process


def finish(process: subprocess.Popen[str], timeout: float = PROCESS_TIMEOUT) -> subprocess.CompletedProcess[str]:
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        terminate(process)
        raise AssertionError(f"ava_tests timed out after {timeout:.1f}s") from error
    ACTIVE_PROCESSES.discard(process)
    return subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)


def run(ava: Path, arguments: list[str], env: dict[str, str], cwd: Path) -> subprocess.CompletedProcess[str]:
    result = finish(start(ava, arguments, env, cwd))
    for prefix in (MARKER_PREFIX, APP_MARKER_PREFIX):
        require(prefix not in result.stdout, f"libcwd marker leaked to stdout: {result.stdout!r}")
        require(prefix not in result.stderr, f"libcwd marker leaked to stderr: {result.stderr!r}")
    return result


def wait_concurrently(processes: list[subprocess.Popen[str]]) -> list[subprocess.CompletedProcess[str]]:
    deadline = time.monotonic() + PROCESS_TIMEOUT
    while any(process.poll() is None for process in processes):
        if time.monotonic() >= deadline:
            for process in processes:
                terminate(process)
            raise AssertionError(f"concurrent ava_tests processes timed out after {PROCESS_TIMEOUT:.1f}s")
        time.sleep(0.01)

    results = [finish(process, timeout=KILL_GRACE) for process in processes]
    for result in results:
        require(MARKER_PREFIX not in result.stdout, f"libcwd marker leaked to concurrent stdout: {result.stdout!r}")
        require(MARKER_PREFIX not in result.stderr, f"libcwd marker leaked to concurrent stderr: {result.stderr!r}")
    return results


def private_file(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")
    path.chmod(0o600)


def log_text(output_directory: Path, suite: str) -> str:
    return named_log_text(output_directory, f"ava_tests.{suite}")


def named_log_text(output_directory: Path, log_stem: str) -> str:
    path = output_directory / f"{log_stem}.libcwd.log"
    require(path.is_file(), f"missing per-suite libcwd log: {path}")
    require(stat.S_IMODE(path.stat().st_mode) == 0o600, f"libcwd log does not have exact mode 0600: {path}")
    return path.read_text(encoding="utf-8")


def marker(suite: str) -> str:
    return MARKER_PREFIX + suite


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ava", type=Path, required=True)
    parser.add_argument("--ava-exe", type=Path, required=True)
    args = parser.parse_args()
    ava = args.ava.resolve(strict=True)
    ava_exe = args.ava_exe.resolve(strict=True)

    previous_handlers = {
        signal_number: signal.signal(signal_number, interrupt_handler)
        for signal_number in (signal.SIGINT, signal.SIGTERM)
    }
    try:
        with tempfile.TemporaryDirectory(prefix="ava-libcwd-output-routing-") as temporary:
            root = Path(temporary)
            require(stat.S_IMODE(root.stat().st_mode) == 0o700, "temporary regression root is not private")

            base_rcfile = root / "libcwdrc.base"
            override_rcfile = root / "libcwdrc.override"
            private_file(base_rcfile, "silent = on\nchannels_default = off\n")
            # The base policy disables every channel. Seeing a NOTICE routing
            # marker therefore proves the caller's override rcfile survived.
            private_file(override_rcfile, "channels_on = notice\n")

            base_env = os.environ.copy()
            base_env.update(
                {
                    "LIBCWD_RCFILE_NAME": str(base_rcfile),
                    "LIBCWD_RCFILE_OVERRIDE_NAME": str(override_rcfile),
                    "LIBCWD_NO_STARTUP_MSGS": "1",
                    # CTest suppresses debug output by default. A nonempty
                    # AVA_DEBUG_OUTPUT_DIR must explicitly override it after the
                    # test executable installs a validated private stream.
                    "AVA_NO_DEBUG_OUTPUT": "1",
                }
            )
            base_env.pop("AVA_DEBUG_OUTPUT_DIR", None)

            default_result = run(ava, ["core_mode"], base_env, root)
            require(default_result.returncode == 0, f"default run failed: {default_result.stderr}")
            require(default_result.stdout == "core_mode tests passed\n", f"unexpected default stdout: {default_result.stdout!r}")
            require(default_result.stderr == "", f"default run wrote stderr: {default_result.stderr!r}")
            require(not list(root.glob("ava_tests.*.libcwd.log")), "default run unexpectedly created a libcwd log")

            empty_env = base_env.copy()
            empty_env["AVA_DEBUG_OUTPUT_DIR"] = ""
            empty_result = run(ava, ["core_mode"], empty_env, root)
            require(empty_result.returncode == 0, f"empty-variable run failed: {empty_result.stderr}")
            require(empty_result.stdout == "core_mode tests passed\n", f"unexpected empty-variable stdout: {empty_result.stdout!r}")
            require(empty_result.stderr == "", f"empty-variable run wrote stderr: {empty_result.stderr!r}")
            require(not list(root.glob("ava_tests.*.libcwd.log")), "empty-variable run unexpectedly created a libcwd log")

            output_directory = root / "debug-output"
            opted_env = base_env.copy()
            opted_env["AVA_DEBUG_OUTPUT_DIR"] = str(output_directory)
            concurrent_processes = [
                start(ava, ["core_mode"], opted_env, root),
                start(ava, ["diagnostics"], opted_env, root),
            ]
            concurrent_results = wait_concurrently(concurrent_processes)
            for suite, result in zip(("core_mode", "diagnostics"), concurrent_results, strict=True):
                require(result.returncode == 0, f"concurrent {suite} run failed: {result.stderr}")
                require(result.stdout == f"{suite} tests passed\n", f"unexpected concurrent {suite} stdout: {result.stdout!r}")
                require(result.stderr == "", f"concurrent {suite} run wrote stderr: {result.stderr!r}")

            require(output_directory.is_dir(), "opt-in output directory was not created")
            require(stat.S_IMODE(output_directory.stat().st_mode) == 0o700, "created output directory is not exact mode 0700")
            core_log = log_text(output_directory, "core_mode")
            diagnostics_log = log_text(output_directory, "diagnostics")
            require(marker("core_mode") in core_log, "core suite routing marker missing from its log")
            require(marker("diagnostics") not in core_log, "diagnostics marker collided with core suite log")
            require(marker("diagnostics") in diagnostics_log, "diagnostics routing marker missing from its log")
            require(marker("core_mode") not in diagnostics_log, "core marker collided with diagnostics suite log")

            real_test_name = "ava_cli.headless_rpc_question_reply"
            real_env = opted_env.copy()
            real_env["AVA_TEST_NAME"] = real_test_name
            real_result = run(ava_exe, ["--help"], real_env, root)
            require(real_result.returncode == 0, f"real ava debug-routing run failed: {real_result.stderr}")
            real_log = named_log_text(output_directory, real_test_name)
            require(
                APP_MARKER_PREFIX + real_test_name in real_log,
                "real ava routing marker missing from its per-test log",
            )

            unsafe_env = opted_env.copy()
            unsafe_env["AVA_TEST_NAME"] = "../escape"
            unsafe_result = run(ava_exe, ["--help"], unsafe_env, root)
            require(unsafe_result.returncode == 0, f"unsafe test-name run failed unexpectedly: {unsafe_result.stderr}")
            require(
                "libcwd log stem must contain only ASCII letters" in unsafe_result.stderr,
                f"unsafe test-name failure was not actionable: {unsafe_result.stderr!r}",
            )
            require(not (root / "escape.libcwd.log").exists(), "unsafe test name escaped AVA_DEBUG_OUTPUT_DIR")

            stale_path = output_directory / "ava_tests.core_mode.libcwd.log"
            stale_path.write_text("STALE-CONTENT\n", encoding="utf-8")
            stale_path.chmod(0o644)
            rerun = run(ava, ["core_mode"], opted_env, root)
            require(rerun.returncode == 0, f"same-suite rerun failed: {rerun.stderr}")
            rerun_log = log_text(output_directory, "core_mode")
            require("STALE-CONTENT" not in rerun_log, "same-suite log was not truncated")
            require(rerun_log.count(marker("core_mode")) == 1, "same-suite routing marker was not deterministic")

            debug_result = run(ava, ["debug"], opted_env, root)
            require(debug_result.returncode == 0, f"debug suite failed: {debug_result.stderr}")
            require("Result:" in debug_result.stdout, "debug suite lost its intentional normal stdout")
            require("debug tests passed" in debug_result.stdout, "debug suite completion output missing")
            require(marker("debug") in log_text(output_directory, "debug"), "debug suite marker was not routed to its suite log")

            invalid_result = run(ava, ["not/a-suite"], opted_env, root)
            require(invalid_result.returncode == 2, "unknown suite did not fail with status 2")
            require(marker("invalid") in log_text(output_directory, "invalid"), "unknown suite did not use the fixed invalid filename token")
            require(not (output_directory / "ava_tests.not").exists(), "unknown argv influenced the output pathname")

            relative_env = base_env.copy()
            relative_env["AVA_DEBUG_OUTPUT_DIR"] = "relative-debug-output"
            relative_result = run(ava, ["core_mode"], relative_env, root)
            require(relative_result.returncode == 2, "relative output directory did not fail with status 2")
            require(
                "AVA_DEBUG_OUTPUT_DIR must be an absolute path" in relative_result.stderr,
                f"relative-path failure was not actionable: {relative_result.stderr!r}",
            )
            require(not (root / "relative-debug-output").exists(), "relative output directory was created")
    finally:
        for process in list(ACTIVE_PROCESSES):
            terminate(process)
        for signal_number, previous in previous_handlers.items():
            signal.signal(signal_number, previous)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
