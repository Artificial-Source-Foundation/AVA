#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys


def private_dir(path: Path):
    path.mkdir(parents=True, exist_ok=True)
    path.chmod(0o700)


def environment(root: Path):
    config = root / "config"
    state = root / "state"
    data = root / "data"
    for path in (root, config, state, data):
        private_dir(path)
    env = os.environ.copy()
    env.update({
        "HOME": str(root / "home"),
        "XDG_CONFIG_HOME": str(config),
        "XDG_STATE_HOME": str(state),
        "XDG_DATA_HOME": str(data),
        "NO_COLOR": "1",
    })
    return env


def run(ava, args, env, stdin=b"", timeout=10):
    return subprocess.run(
        [ava, *args],
        input=stdin,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        timeout=timeout,
        check=False,
    )


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def mode(path: Path):
    return stat.S_IMODE(path.stat().st_mode)


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--ava", required=True)
    parser.add_argument("--root", required=True)
    args = parser.parse_args(argv)

    root = Path(args.root)
    shutil.rmtree(root, ignore_errors=True)
    env = environment(root)
    diagnostics = root / "state" / "ava" / "diagnostics"

    successful = run(args.ava, ["--version"], env)
    require(successful.returncode == 0 and successful.stderr == b"", "default successful process fixture exits cleanly")
    require(not diagnostics.exists(), "default successful process creates no diagnostics artifacts")
    default = run(args.ava, ["--offline", "--no-session", "--print", "default"], env)
    require(default.returncode == 1, "offline default-process fixture should fail without a provider call")
    require(not diagnostics.exists(), "failure outside an admitted runtime creates no diagnostics artifacts")

    processes = [
        subprocess.Popen(
            [args.ava, "--trace", "--offline", "--no-session", "--print", f"trace-{index}"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )
        for index in range(2)
    ]
    for process in processes:
        stdout, _ = process.communicate(timeout=10)
        require(process.returncode == 1 and stdout == b"", "traced print processes keep diagnostics off stdout")
    trace_files = sorted((diagnostics / "traces").glob("trace-v1-*.jsonl"))
    require(len(trace_files) == 2 and trace_files[0] != trace_files[1], "concurrent traced AVA processes publish distinct files")
    for trace in trace_files:
        metadata = trace.stat()
        require(mode(trace) == 0o600 and metadata.st_nlink == 1 and stat.S_ISREG(metadata.st_mode), "trace files are strict private regular files")

    duplicate = run(args.ava, ["--trace", "--trace", "--help"], env)
    require(duplicate.returncode == 2 and duplicate.stdout == b"", "duplicate --trace is a usage error without stdout contamination")

    for order in (["--trace", "--acp"], ["--acp", "--trace"]):
        result = run(args.ava, order, env)
        require(result.returncode == 0 and result.stdout == b"", f"ACP accepts trace ordering {order} without framing contamination")
    for invalid in (["--acp", "--trace", "--trace"], ["--trace", "--acp", "extra"], ["--acp", "--acp"]):
        result = run(args.ava, invalid, env)
        require(result.returncode == 2 and result.stdout == b"", f"ACP rejects invalid trace grammar {invalid}")

    counter = diagnostics / "trace-counters-v1.json"
    require(counter.exists() and mode(counter) == 0o600, "orderly traced mode shutdown persists one private counter snapshot")
    counter_json = json.loads(counter.read_text())
    require(counter_json["schema_version"] == 1, "counter snapshot remains typed")

    last_failure = diagnostics / "last-failure-v1.json"
    last_failure.write_text(
        '{"schema_version":1,"recorded_at":1,"component":"provider","category":"protocol",'
        '"code":"external_failure","retryability":"after_user_action",'
        '"recovery_hint":"Verify the integration configuration before trying again.","occurrences":1}'
    )
    last_failure.chmod(0o600)
    trace_canary = "TRACE_LINE_CANARY_MUST_NOT_EXPORT_7301"
    with trace_files[0].open("a") as output:
        output.write(trace_canary + "\n")

    exported = run(args.ava, ["support", "export"], env)
    require(exported.returncode == 0 and exported.stderr == b"", "support export succeeds")
    match = re.fullmatch(rb"Support artifact created: (.+)\n", exported.stdout)
    require(match is not None, "support export reports the exact local artifact path")
    artifact_path = Path(match.group(1).decode())
    require(artifact_path.exists() and mode(artifact_path) == 0o600, "reported support artifact path names the created private file")
    artifact_text = artifact_path.read_text()
    artifact = json.loads(artifact_text)
    require(str(artifact_path) not in artifact_text, "local support path is not embedded in the artifact")
    require(trace_canary not in artifact_text and "trace-v1-" not in artifact_text, "support export never includes production trace lines or filenames")
    require(artifact["trace"]["state"] == "present" and artifact["last_failure"]["state"] == "present",
            "support export contains only typed counters and safe last failure data")

    print("runtime diagnostics CLI tests passed")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Exception as error:
        print(f"runtime diagnostics CLI test failed: {error}", file=sys.stderr)
        sys.exit(1)
