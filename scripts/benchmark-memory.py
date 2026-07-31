#!/usr/bin/env python3
"""Repeatably compare the idle Linux memory use of AVA, Pi, and OpenCode.

Each program runs offline in an equivalent 120x30 PTY, with an empty project,
an isolated home, and credential-like environment variables removed.  One
warmup is followed by measured runs that aggregate the root process and all of
its descendants using /proc/*/smaps_rollup.

Examples:
  scripts/benchmark-memory.py
  scripts/benchmark-memory.py --apps ava --runs 1 --settle 1 --samples 2
  scripts/benchmark-memory.py --ava build/src/ava/ava --output /tmp/ava.json
  scripts/benchmark-memory.py --apps pi opencode --pi ~/.local/bin/pi
"""

import argparse
import datetime
import fcntl
import json
import math
import os
import platform
import pty
import re
import select
import shutil
import signal
import statistics
import struct
import subprocess
import sys
import tempfile
import termios
import time
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple


PTY_COLUMNS = 120
PTY_ROWS = 30
OUTPUT_LIMIT = 200_000
APP_ARGUMENTS = {
    "ava": ["--offline", "--no-session"],
    "pi": [
        "--offline",
        "--no-session",
        "--no-extensions",
        "--no-skills",
        "--no-prompt-templates",
        "--no-themes",
        "--no-context-files",
    ],
    "opencode": ["--pure"],
}
SENSITIVE_ENVIRONMENT_NAME = re.compile(
    r"(?:API[_-]?KEY|TOKEN|SECRET|CREDENTIAL|PASSWORD|PASSWD|NETRC|SSH_AUTH|"
    r"OPENAI|ANTHROPIC|GEMINI|GOOGLE_AI|AZURE|AWS_|BEDROCK|VERTEX|COHERE|"
    r"MISTRAL|GROQ|HUGGINGFACE|HF_TOKEN|GITHUB_TOKEN|GITLAB_TOKEN|CLOUDFLARE)",
    re.IGNORECASE,
)
ANSI_ESCAPE = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1b\\))")
METRIC_KEYS = ("pss_kib", "rss_kib", "uss_kib", "swap_kib", "processes", "threads")


def parse_proc_stat(stat_text: str) -> Tuple[int, str]:
    """Return (ppid, comm), accounting for spaces and ')' inside comm."""
    opening = stat_text.find("(")
    closing = stat_text.rfind(")")
    if opening < 0 or closing <= opening:
        raise ValueError("malformed /proc stat record")
    fields_after_comm = stat_text[closing + 1 :].split()
    # Fields after comm begin with field 3 (state), then field 4 (ppid).
    if len(fields_after_comm) < 2:
        raise ValueError("short /proc stat record")
    return int(fields_after_comm[1]), stat_text[opening + 1 : closing]


def descendant_pids(root_pid: int) -> List[int]:
    parents: Dict[int, int] = {}
    try:
        proc_entries = list(Path("/proc").iterdir())
    except OSError:
        proc_entries = []
    for entry in proc_entries:
        if not entry.name.isdigit():
            continue
        try:
            ppid, _ = parse_proc_stat((entry / "stat").read_text())
            parents[int(entry.name)] = ppid
        except (OSError, ValueError):
            continue

    found = {root_pid}
    while True:
        additions = {pid for pid, ppid in parents.items() if ppid in found} - found
        if not additions:
            return sorted(found)
        found.update(additions)


def read_process_tree(root_pid: int) -> Dict[str, object]:
    total: Dict[str, object] = {
        "pss_kib": 0,
        "rss_kib": 0,
        "uss_kib": 0,
        "swap_kib": 0,
        "processes": 0,
        "threads": 0,
        "process_names": [],
    }
    names: List[str] = []
    for pid in descendant_pids(root_pid):
        try:
            smaps: Dict[str, int] = {}
            for line in Path("/proc", str(pid), "smaps_rollup").read_text().splitlines():
                fields = line.split()
                if len(fields) >= 2 and fields[0].endswith(":"):
                    smaps[fields[0][:-1]] = int(fields[1])

            status: Dict[str, str] = {}
            for line in Path("/proc", str(pid), "status").read_text().splitlines():
                if ":" in line:
                    key, value = line.split(":", 1)
                    status[key] = value.strip()

            total["pss_kib"] += smaps.get("Pss", 0)  # type: ignore[operator]
            total["rss_kib"] += smaps.get("Rss", 0)  # type: ignore[operator]
            total["uss_kib"] += (  # type: ignore[operator]
                smaps.get("Private_Clean", 0)
                + smaps.get("Private_Dirty", 0)
                + smaps.get("Private_Hugetlb", 0)
            )
            total["swap_kib"] += smaps.get("Swap", 0)  # type: ignore[operator]
            total["threads"] += int(status.get("Threads", "0"))  # type: ignore[operator]
            total["processes"] += 1  # type: ignore[operator]
            names.append(status.get("Name", str(pid)))
        except (OSError, ValueError):
            # Processes can legitimately exit while /proc is being sampled.
            continue
    total["process_names"] = names
    return total


def isolated_environment(home: Path, app: Optional[str] = None) -> Dict[str, str]:
    replaced = {
        "HOME",
        "XDG_CONFIG_HOME",
        "XDG_CACHE_HOME",
        "XDG_DATA_HOME",
        "XDG_STATE_HOME",
        "TMPDIR",
        "TERM",
        "COLORTERM",
    }
    environment = {
        key: value
        for key, value in os.environ.items()
        if key not in replaced and not SENSITIVE_ENVIRONMENT_NAME.search(key)
    }
    environment.update(
        {
            "HOME": str(home),
            "XDG_CONFIG_HOME": str(home / ".config"),
            "XDG_CACHE_HOME": str(home / ".cache"),
            "XDG_DATA_HOME": str(home / ".local" / "share"),
            "XDG_STATE_HOME": str(home / ".local" / "state"),
            "TMPDIR": str(home / "tmp"),
            "TERM": "xterm-256color",
            "COLORTERM": "truecolor",
            "NO_COLOR": "1",
        }
    )
    if app == "opencode":
        environment.update(
            {
                "OPENCODE_DISABLE_AUTOUPDATE": "true",
                "OPENCODE_DISABLE_MODELS_FETCH": "true",
                "OPENCODE_DISABLE_LSP_DOWNLOAD": "true",
            }
        )
    return environment


def drain_pty(fd: int, output: bytearray) -> bool:
    truncated = False
    while True:
        try:
            readable, _, _ = select.select([fd], [], [], 0)
        except (OSError, ValueError):
            return truncated
        if not readable:
            return truncated
        try:
            chunk = os.read(fd, 65_536)
        except (BlockingIOError, OSError):
            return truncated
        if not chunk:
            return truncated
        output.extend(chunk)
        if len(output) > OUTPUT_LIMIT:
            del output[:-OUTPUT_LIMIT]
            truncated = True


def process_group_exists(process_group: int) -> bool:
    try:
        os.killpg(process_group, 0)
        return True
    except ProcessLookupError:
        return False


def terminate_process_group(process: subprocess.Popen[bytes], master_fd: int, output: bytearray) -> None:
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    deadline = time.monotonic() + 3.0
    while process_group_exists(process.pid) and time.monotonic() < deadline:
        drain_pty(master_fd, output)
        if process.poll() is None:
            try:
                process.wait(timeout=min(0.05, max(0.0, deadline - time.monotonic())))
            except subprocess.TimeoutExpired:
                pass
        else:
            time.sleep(0.05)
    if process_group_exists(process.pid):
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    try:
        process.wait(timeout=3.0)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"process group {process.pid} survived SIGKILL") from error


def summarize_samples(samples: Sequence[Dict[str, object]]) -> Dict[str, object]:
    summary: Dict[str, object] = {}
    for key in METRIC_KEYS:
        summary[key] = round(statistics.median(float(sample[key]) for sample in samples), 1)
    summary["process_names"] = samples[-1]["process_names"]
    return summary


def run_once(
    app: str,
    command: Sequence[str],
    home: Path,
    project: Path,
    settle_seconds: float,
    sample_count: int,
    sample_interval_seconds: float,
) -> Dict[str, object]:
    master_fd, slave_fd = pty.openpty()
    process: Optional[subprocess.Popen[bytes]] = None
    output = bytearray()
    output_truncated = False
    started = time.monotonic()
    full_command = list(command) + ([str(project)] if app == "opencode" else [])
    try:
        fcntl.ioctl(
            slave_fd,
            termios.TIOCSWINSZ,
            struct.pack("HHHH", PTY_ROWS, PTY_COLUMNS, 0, 0),
        )
        flags = fcntl.fcntl(master_fd, fcntl.F_GETFL)
        fcntl.fcntl(master_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
        process = subprocess.Popen(
            full_command,
            cwd=project,
            env=isolated_environment(home, app),
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            start_new_session=True,
            close_fds=True,
        )
        os.close(slave_fd)
        slave_fd = -1

        settle_deadline = time.monotonic() + settle_seconds
        while process.poll() is None and time.monotonic() < settle_deadline:
            output_truncated |= drain_pty(master_fd, output)
            time.sleep(min(0.05, max(0.0, settle_deadline - time.monotonic())))

        samples: List[Dict[str, object]] = []
        if process.poll() is None:
            for sample_index in range(sample_count):
                output_truncated |= drain_pty(master_fd, output)
                reading = read_process_tree(process.pid)
                if int(reading["processes"]) == 0:
                    break
                reading["sample_index"] = sample_index + 1
                reading["elapsed_seconds"] = round(time.monotonic() - started, 3)
                samples.append(reading)
                if sample_index + 1 < sample_count:
                    deadline = time.monotonic() + sample_interval_seconds
                    while process.poll() is None and time.monotonic() < deadline:
                        output_truncated |= drain_pty(master_fd, output)
                        time.sleep(min(0.05, max(0.0, deadline - time.monotonic())))

        return_code = process.poll()
        if not samples:
            text = ANSI_ESCAPE.sub("", bytes(output).decode("utf-8", "replace"))
            raise RuntimeError(
                f"{app} exited before measurement (return code {return_code}): "
                f"{text[-1200:].strip()}"
            )
        result = {
            "summary": summarize_samples(samples),
            "samples": samples,
            "elapsed_seconds": round(time.monotonic() - started, 3),
            "captured_output_bytes": len(output),
            "captured_output_truncated": output_truncated,
        }
        return result
    finally:
        if process is not None:
            terminate_process_group(process, master_fd, output)
        if slave_fd >= 0:
            os.close(slave_fd)
        os.close(master_fd)


def executable_version(executable: str, home: Path, project: Path, app: str) -> Dict[str, object]:
    process: Optional[subprocess.Popen[bytes]] = None
    try:
        process = subprocess.Popen(
            [executable, "--version"],
            cwd=project,
            env=isolated_environment(home, app),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            output, _ = process.communicate(timeout=3.0)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            output, _ = process.communicate(timeout=3.0)
            return {"error": "TimeoutExpired"}
        text = ANSI_ESCAPE.sub("", output[:4096].decode("utf-8", "replace")).strip()
        return {"return_code": process.returncode, "output": text}
    except OSError as error:
        if process is not None and process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=3.0)
        return {"error": type(error).__name__}


def resolve_executable(app: str, override: Optional[str]) -> str:
    requested = override or app
    resolved = shutil.which(os.path.expanduser(requested))
    if resolved is None:
        source = f"--{app} {override}" if override else f"PATH ({app})"
        raise RuntimeError(f"requested {app} executable was not found via {source}")
    return str(Path(resolved).resolve())


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def nonnegative_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed < 0:
        raise argparse.ArgumentTypeError("must be a finite number that is zero or greater")
    return parsed


def default_output_path() -> Path:
    timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    return Path(tempfile.gettempdir(), f"ava-memory-benchmark-{timestamp}-{os.getpid()}.json")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare idle memory for AVA, Pi, and OpenCode without provider calls.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""examples:
  %(prog)s
  %(prog)s --apps ava --runs 1 --settle 1 --samples 2
  %(prog)s --ava build/src/ava/ava --output /tmp/ava-memory.json
  %(prog)s --apps pi opencode --sample-interval 0.25
""",
    )
    parser.add_argument(
        "--apps",
        nargs="+",
        choices=APP_ARGUMENTS,
        default=list(APP_ARGUMENTS),
        help="apps to benchmark (default: all)",
    )
    parser.add_argument("--ava", metavar="PATH", help="AVA executable override")
    parser.add_argument("--pi", metavar="PATH", help="Pi executable override")
    parser.add_argument("--opencode", metavar="PATH", help="OpenCode executable override")
    parser.add_argument(
        "--runs",
        type=positive_int,
        default=5,
        help="measured runs after one warmup (default: 5)",
    )
    parser.add_argument(
        "--settle",
        type=nonnegative_float,
        default=5.0,
        metavar="SECONDS",
        help="idle settling time per run (default: 5)",
    )
    parser.add_argument(
        "--samples",
        type=positive_int,
        default=3,
        help="smaps samples per measured run (default: 3)",
    )
    parser.add_argument(
        "--sample-interval",
        type=nonnegative_float,
        default=0.5,
        metavar="SECONDS",
        help="delay between samples (default: 0.5)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="JSON output path (default: timestamped file in the system temporary directory)",
    )
    return parser.parse_args()


def format_memory(kib: object) -> str:
    return f"{float(kib) / 1024:.1f} MiB"


def main() -> int:
    args = parse_arguments()
    if sys.platform != "linux":
        raise RuntimeError("this benchmark supports Linux only")
    try:
        Path("/proc/self/smaps_rollup").read_text()
    except OSError as error:
        raise RuntimeError("/proc/self/smaps_rollup is not readable; procfs smaps access is required") from error

    commands: Dict[str, List[str]] = {}
    for app in args.apps:
        executable = resolve_executable(app, getattr(args, app))
        commands[app] = [executable] + APP_ARGUMENTS[app]

    output_path = (args.output or default_output_path()).expanduser().resolve()
    root = Path(tempfile.mkdtemp(prefix="ava-memory-benchmark-"))
    project = root / "project"
    project.mkdir()
    # A filesystem-only marker prevents tools from discovering the source checkout.
    (project / ".git").mkdir()

    result: Dict[str, object] = {
        "schema_version": 1,
        "generated_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "parameters": {
            "measured_runs": args.runs,
            "warmup_runs": 1,
            "settle_seconds": args.settle,
            "samples_per_run": args.samples,
            "sample_interval_seconds": args.sample_interval,
            "pty_columns": PTY_COLUMNS,
            "pty_rows": PTY_ROWS,
            "project": "empty isolated temporary directory",
            "environment": "isolated HOME/XDG/TMPDIR; credential-like variables removed; offline flags",
        },
        "host": {
            "platform": platform.platform(),
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
            "cpu_count": os.cpu_count(),
        },
        "apps": {},
    }

    try:
        for app in args.apps:
            home = root / f"home-{app}"
            home.mkdir()
            (home / "tmp").mkdir()
            command = commands[app]
            full_command = command + ([str(project)] if app == "opencode" else [])
            print(f"warming {app}: {' '.join(full_command)}", flush=True)
            warmup = run_once(app, command, home, project, args.settle, 1, args.sample_interval)
            warm_summary = warmup["summary"]
            print(f"  warm PSS={format_memory(warm_summary['pss_kib'])}", flush=True)  # type: ignore[index]

            runs = []
            for run_index in range(args.runs):
                run = run_once(
                    app,
                    command,
                    home,
                    project,
                    args.settle,
                    args.samples,
                    args.sample_interval,
                )
                run["run_index"] = run_index + 1
                runs.append(run)
                summary = run["summary"]
                print(  # type: ignore[index]
                    f"  run {run_index + 1}: PSS={format_memory(summary['pss_kib'])} "
                    f"RSS={format_memory(summary['rss_kib'])} "
                    f"USS={format_memory(summary['uss_kib'])} "
                    f"swap={format_memory(summary['swap_kib'])} "
                    f"procs={summary['processes']:.0f} threads={summary['threads']:.0f}",
                    flush=True,
                )

            aggregate = {
                key: round(statistics.median(float(run["summary"][key]) for run in runs), 1)  # type: ignore[index]
                for key in METRIC_KEYS
            }
            result["apps"][app] = {  # type: ignore[index]
                "resolved_command": full_command,
                "version": executable_version(command[0], home, project, app),
                "warmup": warmup,
                "runs": runs,
                "aggregate_median": aggregate,
            }

        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        print("\nsummary (median of run medians):")
        for app in args.apps:
            aggregate = result["apps"][app]["aggregate_median"]  # type: ignore[index]
            print(
                f"  {app:8} PSS {format_memory(aggregate['pss_kib']):>10}  "
                f"RSS {format_memory(aggregate['rss_kib']):>10}  "
                f"USS {format_memory(aggregate['uss_kib']):>10}  "
                f"swap {format_memory(aggregate['swap_kib']):>10}  "
                f"procs {aggregate['processes']:.0f}  threads {aggregate['threads']:.0f}"
            )
        print(f"results: {output_path}")
        return 0
    finally:
        shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("benchmark interrupted", file=sys.stderr)
        sys.exit(130)
    except (OSError, RuntimeError) as error:
        print(f"benchmark-memory.py: error: {error}", file=sys.stderr)
        sys.exit(1)
