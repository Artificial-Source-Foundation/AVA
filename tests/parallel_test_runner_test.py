#!/usr/bin/env python3
"""Contract tests for AVA's parallel build/test wrappers without doing real work."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_runner(script: Path, build_dir: Path, fake_ctest: Path, *args: str, env_extra: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env.pop("CTEST_PARALLEL_LEVEL", None)
    env["AVA_CTEST_COMMAND"] = str(fake_ctest)
    if env_extra:
        env.update(env_extra)
    return subprocess.run(
        [str(script), "--build-dir", str(build_dir), *args],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        check=False,
        timeout=10,
    )


def run_build_runner(
    script: Path,
    build_dir: Path,
    fake_cmake: Path,
    *args: str,
    env_extra: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env.pop("CMAKE_BUILD_PARALLEL_LEVEL", None)
    env["AVA_CMAKE_COMMAND"] = str(fake_cmake)
    if env_extra:
        env.update(env_extra)
    return subprocess.run(
        [str(script), "--build-dir", str(build_dir), *args],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        check=False,
        timeout=10,
    )


def wait_for_path(path: Path, process: subprocess.Popen[str], timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        require(process.poll() is None, f"runner exited before creating synchronization marker: {process.returncode}")
        time.sleep(0.01)
    raise AssertionError(f"timed out waiting for synchronization marker: {path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", type=Path, required=True)
    parser.add_argument("--build-script", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()

    script = args.script.resolve(strict=True)
    build_script = args.build_script.resolve(strict=True)
    root = args.root.resolve()
    shutil.rmtree(root, ignore_errors=True)
    build_dir = root / "build"
    build_dir.mkdir(parents=True)
    (build_dir / "CTestTestfile.cmake").write_text("# fake configured tree\n", encoding="utf-8")
    (build_dir / "CMakeCache.txt").write_text("# fake configured tree\n", encoding="utf-8")

    invocation_file = root / "ctest-args.json"
    fake_ctest = root / "fake-ctest.py"
    fake_ctest.write_text(
        """#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys
import time

ready = os.environ.get("FAKE_CTEST_READY")
release = os.environ.get("FAKE_CTEST_RELEASE")
if ready:
    Path(ready).write_text("ready\\n", encoding="utf-8")
if release:
    deadline = time.monotonic() + 8
    while not Path(release).exists():
        if time.monotonic() >= deadline:
            raise SystemExit(9)
        time.sleep(0.01)
Path(os.environ["FAKE_CTEST_ARGS"]).write_text(json.dumps(sys.argv[1:]), encoding="utf-8")
""",
        encoding="utf-8",
    )
    fake_ctest.chmod(0o755)

    common_env = {"FAKE_CTEST_ARGS": str(invocation_file)}
    explicit = run_runner(script, build_dir, fake_ctest, "--jobs", "7", "-R", "^ava_tests\\.lsp$", env_extra=common_env)
    require(explicit.returncode == 0, f"explicit runner failed: {explicit.stderr}")
    require("7 parallel jobs" in explicit.stdout, "runner reports the selected parallel level")
    require(
        json.loads(invocation_file.read_text(encoding="utf-8"))
        == ["--test-dir", str(build_dir), "--output-on-failure", "--parallel", "7", "-R", "^ava_tests\\.lsp$"],
        "runner forwards the build tree, all-core option, and CTest filters exactly",
    )

    invocation_file.unlink()
    inherited = run_runner(
        script,
        build_dir,
        fake_ctest,
        "--output-junit",
        str(root / "results.xml"),
        env_extra={**common_env, "CTEST_PARALLEL_LEVEL": "3"},
    )
    require(inherited.returncode == 0, f"environment override failed: {inherited.stderr}")
    require(json.loads(invocation_file.read_text(encoding="utf-8"))[3:5] == ["--parallel", "3"], "runner honors CTEST_PARALLEL_LEVEL")

    invocation_file.unlink()
    invalid = run_runner(script, build_dir, fake_ctest, "--jobs", "0", env_extra=common_env)
    require(invalid.returncode == 2, "runner rejects unbounded/zero parallelism")
    require("positive integer" in invalid.stderr and not invocation_file.exists(), "invalid jobs fail before CTest starts")

    build_invocation_file = root / "cmake-args.json"
    build_env = {"FAKE_CTEST_ARGS": str(build_invocation_file)}
    explicit_build = run_build_runner(
        build_script,
        build_dir,
        fake_ctest,
        "--jobs",
        "6",
        "--target",
        "ava_tests",
        env_extra=build_env,
    )
    require(explicit_build.returncode == 0, f"explicit build runner failed: {explicit_build.stderr}")
    require("6 parallel jobs" in explicit_build.stdout, "build runner reports the selected parallel level")
    require(
        json.loads(build_invocation_file.read_text(encoding="utf-8"))
        == ["--build", str(build_dir), "--parallel", "6", "--target", "ava_tests"],
        "build runner forwards the build tree, parallel level, and target exactly",
    )

    build_invocation_file.unlink()
    inherited_build = run_build_runner(
        build_script,
        build_dir,
        fake_ctest,
        "--verbose",
        env_extra={**build_env, "CMAKE_BUILD_PARALLEL_LEVEL": "4"},
    )
    require(inherited_build.returncode == 0, f"build environment override failed: {inherited_build.stderr}")
    require(
        json.loads(build_invocation_file.read_text(encoding="utf-8"))[2:4] == ["--parallel", "4"],
        "build runner honors CMAKE_BUILD_PARALLEL_LEVEL",
    )

    build_invocation_file.unlink()
    invalid_build = run_build_runner(build_script, build_dir, fake_ctest, "--jobs", "0", env_extra=build_env)
    require(invalid_build.returncode == 2, "build runner rejects unbounded/zero parallelism")
    require(
        "positive integer" in invalid_build.stderr and not build_invocation_file.exists(),
        "invalid build jobs fail before CMake starts",
    )

    ready = root / "ready"
    release = root / "release"
    first_env = os.environ.copy()
    first_env.pop("CTEST_PARALLEL_LEVEL", None)
    first_env.update(
        {
            "AVA_CTEST_COMMAND": str(fake_ctest),
            "FAKE_CTEST_ARGS": str(invocation_file),
            "FAKE_CTEST_READY": str(ready),
            "FAKE_CTEST_RELEASE": str(release),
        }
    )
    first = subprocess.Popen(
        [str(script), "--build-dir", str(build_dir), "--jobs", "2"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=first_env,
    )
    try:
        wait_for_path(ready, first, 5)
        second = run_runner(script, build_dir, fake_ctest, "--jobs", "2", env_extra=common_env)
        require(second.returncode == 2 and "already owns build tree" in second.stderr, "runner rejects a concurrent test run in the same build tree")
        blocked_build = run_build_runner(build_script, build_dir, fake_ctest, "--jobs", "2", env_extra=build_env)
        require(
            blocked_build.returncode == 2 and "already owns build tree" in blocked_build.stderr,
            "build runner shares the build-tree lock with the test runner",
        )
        release.write_text("release\n", encoding="utf-8")
        first_stdout, first_stderr = first.communicate(timeout=10)
        require(first.returncode == 0, f"locked runner failed after release: {first_stdout}\n{first_stderr}")
    finally:
        if first.poll() is None:
            first.kill()
            first.wait(timeout=5)

    ready.unlink()
    release.unlink()
    signaled = subprocess.Popen(
        [str(script), "--build-dir", str(build_dir), "--jobs", "2"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=first_env,
    )
    try:
        wait_for_path(ready, signaled, 5)
        signaled.terminate()
        signal_stdout, signal_stderr = signaled.communicate(timeout=10)
        require(signaled.returncode in (-15, 143), f"terminated runner returned {signaled.returncode}: {signal_stdout}\n{signal_stderr}")
        after_signal = run_runner(script, build_dir, fake_ctest, "--jobs", "2", env_extra=common_env)
        require(after_signal.returncode == 0, f"terminated runner retained its build lock: {after_signal.stderr}")
    finally:
        if signaled.poll() is None:
            signaled.kill()
            signaled.wait(timeout=5)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
