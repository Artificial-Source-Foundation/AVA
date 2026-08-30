#!/usr/bin/env python3
"""Run AVA's offline pre-modernization backend benchmark suites.

The harness is dependency-free, isolates process state, records every requested
M0 family, and writes both machine-readable JSON and an optional Markdown view.
It does not make cross-machine performance claims.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Sequence

SCHEMA_VERSION = "ava.backend-benchmark.v1"
SENSITIVE_ENVIRONMENT_NAME = re.compile(
    r"(?:API[_-]?KEY|TOKEN|SECRET|CREDENTIAL|PASSWORD|PASSWD|NETRC|SSH_AUTH|"
    r"OPENAI|ANTHROPIC|GEMINI|GOOGLE_AI|AZURE|AWS_|BEDROCK|VERTEX|COHERE|"
    r"MISTRAL|GROQ|HUGGINGFACE|HF_TOKEN|GITHUB_TOKEN|GITLAB_TOKEN|CLOUDFLARE)",
    re.IGNORECASE,
)
NETWORK_ENVIRONMENT_NAME = re.compile(r"(?:^|_)(?:HTTP|HTTPS|ALL|NO)_PROXY$", re.IGNORECASE)
EXPECTED_RESULT_IDS = (
    "cold_startup",
    "warm_startup",
    "idle_rss",
    "builtin_noop_dispatch",
    "native_file_read_dispatch",
    "native_registry_schema_selection",
    "plugin_first_call",
    "plugin_repeated_calls",
    "unused_plugin_manifests_100",
    "catalog_entries_100",
    "catalog_entries_500",
    "catalog_entries_1000",
    "short_calls_10000",
    "canceled_calls_10000",
    "session_open_1000",
    "session_open_10000",
    "session_open_100000",
    "metadata_browsing",
    "cancellation_acknowledgement",
    "child_cleanup",
    "repeated_call_memory",
    "selective_router",
)


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run reproducible offline AVA backend benchmarks.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""examples:
  scripts/benchmark-backend.py --ava build/ava --benchmark-helper build/tests/ava_backend_benchmark_helper --suite smoke --runs 1 --output /tmp/ava-smoke.json
  scripts/benchmark-backend.py --ava build-release/ava --benchmark-helper build-release/tests/ava_backend_benchmark_helper --suite baseline --runs 5 --output baseline.json --report baseline.md
  scripts/benchmark-backend.py --ava build-release/ava --benchmark-helper build-release/tests/ava_backend_benchmark_helper --suite stress --runs 10 --output stress.json
""",
    )
    parser.add_argument("--ava", type=Path, required=True, help="path to the AVA executable")
    parser.add_argument("--benchmark-helper", type=Path, help="test-only deterministic C++ benchmark helper")
    parser.add_argument("--fake-provider", type=Path, help="optional local fake provider executable (recorded; no network provider is used)")
    parser.add_argument(
        "--memory-helper",
        type=Path,
        default=Path(__file__).with_name("benchmark-memory.py"),
        help="existing idle-memory helper (default: scripts/benchmark-memory.py)",
    )
    parser.add_argument(
        "--sample-plugin",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "examples" / "plugins" / "todo",
        help="sample todo plugin fixture",
    )
    parser.add_argument("--suite", choices=("smoke", "baseline", "stress"), required=True)
    parser.add_argument("--runs", type=positive_int, required=True, help="measured repetitions")
    parser.add_argument("--output", type=Path, required=True, help="machine-readable JSON output")
    parser.add_argument("--report", type=Path, help="optional readable Markdown report")
    return parser


def resolve_file(path: Path, description: str, executable: bool = False) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise ValueError(f"{description} is not a file: {resolved}")
    if executable and not os.access(resolved, os.X_OK):
        raise ValueError(f"{description} is not executable: {resolved}")
    return resolved


def validate_arguments(args: argparse.Namespace) -> None:
    args.ava = resolve_file(args.ava, "--ava", executable=True)
    if args.benchmark_helper is not None:
        args.benchmark_helper = resolve_file(args.benchmark_helper, "--benchmark-helper", executable=True)
    if args.fake_provider is not None:
        args.fake_provider = resolve_file(args.fake_provider, "--fake-provider", executable=True)
    args.memory_helper = resolve_file(args.memory_helper, "--memory-helper")
    args.sample_plugin = args.sample_plugin.expanduser().resolve()
    if not (args.sample_plugin / "plugin.json").is_file() or not (args.sample_plugin / "plugin.sh").is_file():
        raise ValueError(f"--sample-plugin must contain plugin.json and plugin.sh: {args.sample_plugin}")
    args.output = args.output.expanduser().resolve()
    if args.report is not None:
        args.report = args.report.expanduser().resolve()
    if args.report == args.output:
        raise ValueError("--report and --output must be different paths")


def isolated_environment(home: Path) -> dict[str, str]:
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
        if key not in replaced
        and not SENSITIVE_ENVIRONMENT_NAME.search(key)
        and not NETWORK_ENVIRONMENT_NAME.search(key)
    }
    environment.update(
        {
            "HOME": str(home),
            "XDG_CONFIG_HOME": str(home / ".config"),
            "XDG_CACHE_HOME": str(home / ".cache"),
            "XDG_DATA_HOME": str(home / ".local" / "share"),
            "XDG_STATE_HOME": str(home / ".local" / "state"),
            "TMPDIR": str(home / "tmp"),
            "TERM": "dumb",
            "NO_COLOR": "1",
            "AVA_BENCHMARK_OFFLINE": "1",
        }
    )
    return environment


def percentile_95(values: Sequence[float]) -> float:
    if not values:
        raise ValueError("cannot summarize no samples")
    ordered = sorted(values)
    return ordered[max(0, math.ceil(0.95 * len(ordered)) - 1)]


def summarize(values: Sequence[float]) -> dict[str, float]:
    if not values:
        raise ValueError("cannot summarize no samples")
    return {
        "median": float(statistics.median(values)),
        "p95": float(percentile_95(values)),
        "maximum": float(max(values)),
    }


def unsupported(result_id: str, family: str, reason: str, reason_code: str, closest: str | None = None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "id": result_id,
        "family": family,
        "status": "unsupported",
        "reason_code": reason_code,
        "reason": reason,
        "repetitions": 0,
        "samples": [],
        "statistics": None,
    }
    if closest:
        result["closest_current_result"] = closest
    return result


def measured_result(
    result_id: str,
    family: str,
    status: str,
    unit: str,
    samples: Sequence[dict[str, Any]],
    command: Sequence[str],
    note: str = "",
) -> dict[str, Any]:
    values = [float(sample["value"]) for sample in samples]
    result = {
        "id": result_id,
        "family": family,
        "status": status,
        "unit": unit,
        "exact_command": list(command),
        "repetitions": len(samples),
        "samples": list(samples),
        "statistics": summarize(values),
    }
    if note:
        result["note"] = note
    return result


def run_process(command: Sequence[str], cwd: Path, environment: dict[str, str], timeout: float) -> tuple[float, subprocess.CompletedProcess[str]]:
    started = time.monotonic_ns()
    completed = subprocess.run(
        list(command),
        cwd=cwd,
        env=environment,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
        check=False,
    )
    elapsed = float(time.monotonic_ns() - started)
    return elapsed, completed


def run_startup(args: argparse.Namespace, root: Path, project: Path) -> dict[str, Any]:
    command = [str(args.ava), "--rpc", "--offline", "--no-session"]
    samples: list[dict[str, Any]] = []
    # One unrecorded run makes the measured population explicitly warm.
    warm_home = root / "startup-warmup"
    prepare_home(warm_home)
    _, warmup = run_process(command, project, isolated_environment(warm_home), 20.0)
    if warmup.returncode != 0:
        raise RuntimeError(f"AVA startup warmup failed ({warmup.returncode}): {warmup.stderr[-1000:]}")
    for index in range(args.runs):
        home = root / f"startup-{index}"
        prepare_home(home)
        elapsed, completed = run_process(command, project, isolated_environment(home), 20.0)
        if completed.returncode != 0:
            raise RuntimeError(f"AVA startup failed ({completed.returncode}): {completed.stderr[-1000:]}")
        samples.append({"run": index + 1, "value": elapsed, "return_code": completed.returncode})
    return measured_result(
        "warm_startup",
        "startup",
        "measured",
        "ns",
        samples,
        command,
        "Full offline RPC initialization and clean EOF shutdown after one warm-up; it performs no provider request.",
    )


def prepare_home(home: Path) -> None:
    for relative in ("tmp", ".config", ".cache", ".local/share", ".local/state"):
        (home / relative).mkdir(parents=True, exist_ok=True)


def run_helper(
    args: argparse.Namespace,
    root: Path,
    project: Path,
    result_id: str,
    family: str,
    benchmark_case: str,
    extra: Sequence[str],
    status: str = "measured",
    note: str = "",
) -> dict[str, Any]:
    if args.benchmark_helper is None:
        return unsupported(result_id, family, "The test-only C++ benchmark helper was not supplied.", "missing_benchmark_helper")
    command = [str(args.benchmark_helper), "--case", benchmark_case, *extra]
    samples: list[dict[str, Any]] = []
    unit: str | None = None
    for index in range(args.runs):
        home = root / f"helper-{result_id}-{index}"
        prepare_home(home)
        wall_ns, completed = run_process(command, project, isolated_environment(home), 60.0 if args.suite != "stress" else 300.0)
        if completed.returncode != 0:
            raise RuntimeError(f"benchmark helper {result_id} failed ({completed.returncode}): {completed.stderr[-2000:]}")
        try:
            payload = json.loads(completed.stdout)
            value = float(payload["value"])
            current_unit = str(payload["unit"])
        except (KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
            raise RuntimeError(f"benchmark helper {result_id} emitted invalid JSON: {completed.stdout[-1000:]}") from error
        if unit is not None and unit != current_unit:
            raise RuntimeError(f"benchmark helper {result_id} changed units")
        unit = current_unit
        samples.append(
            {
                "run": index + 1,
                "value": value,
                "wall_time_ns": wall_ns,
                "details": payload.get("details", {}),
            }
        )
    assert unit is not None
    return measured_result(result_id, family, status, unit, samples, command, note)


def run_idle_memory(args: argparse.Namespace, root: Path, project: Path) -> dict[str, Any]:
    if sys.platform != "linux" or not Path("/proc/self/smaps_rollup").is_file():
        return unsupported("idle_rss", "memory", "Idle process-tree RSS requires Linux procfs smaps_rollup.", "procfs_unavailable")
    output = root / "idle-memory.json"
    settle = {"smoke": "0.1", "baseline": "1.0", "stress": "3.0"}[args.suite]
    sample_count = "1" if args.suite == "smoke" else "3"
    command = [
        sys.executable,
        str(args.memory_helper),
        "--apps",
        "ava",
        "--ava",
        str(args.ava),
        "--runs",
        str(args.runs),
        "--settle",
        settle,
        "--samples",
        sample_count,
        "--output",
        str(output),
    ]
    home = root / "memory-driver"
    prepare_home(home)
    _, completed = run_process(command, project, isolated_environment(home), 120.0 if args.suite == "smoke" else 600.0)
    if completed.returncode != 0:
        raise RuntimeError(f"idle memory helper failed ({completed.returncode}): {completed.stderr[-2000:]}")
    payload = json.loads(output.read_text(encoding="utf-8"))
    runs = payload["apps"]["ava"]["runs"]
    samples = [
        {
            "run": index + 1,
            "value": float(run["summary"]["rss_kib"]),
            "processes": float(run["summary"]["processes"]),
            "pss_kib": float(run["summary"]["pss_kib"]),
            "uss_kib": float(run["summary"]["uss_kib"]),
        }
        for index, run in enumerate(runs)
    ]
    return measured_result(
        "idle_rss",
        "memory",
        "measured",
        "KiB",
        samples,
        command,
        "Linux process-tree RSS from the existing procfs/PTY memory seam; includes AVA descendants.",
    )


def not_selected(result_id: str, family: str, suite: str, closest: str | None = None) -> dict[str, Any]:
    return unsupported(
        result_id,
        family,
        f"The {suite} suite intentionally excludes this scale point; use --suite baseline or stress.",
        "not_selected_by_suite",
        closest,
    )


def git_identity(repository: Path) -> dict[str, Any]:
    def git(*arguments: str) -> str:
        completed = subprocess.run(
            ["git", "-C", str(repository), *arguments],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=10,
            check=False,
        )
        if completed.returncode != 0:
            return "unknown"
        return completed.stdout.strip()

    commit = git("rev-parse", "HEAD")
    status = git("status", "--porcelain", "--untracked-files=normal")
    dirty = status not in ("", "unknown")
    return {"commit": commit, "dirty": dirty, "commit_with_state": f"{commit}{'-dirty' if dirty else ''}"}


def total_ram_bytes() -> int | None:
    try:
        if sys.platform == "linux":
            for line in Path("/proc/meminfo").read_text(encoding="utf-8").splitlines():
                if line.startswith("MemTotal:"):
                    return int(line.split()[1]) * 1024
        page_size = os.sysconf("SC_PAGE_SIZE")
        page_count = os.sysconf("SC_PHYS_PAGES")
        return int(page_size) * int(page_count)
    except (OSError, ValueError, KeyError, AttributeError):
        return None


def cpu_model() -> str:
    try:
        if sys.platform == "linux":
            for line in Path("/proc/cpuinfo").read_text(encoding="utf-8", errors="replace").splitlines():
                if line.lower().startswith("model name"):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def find_cmake_cache(binary: Path) -> Path | None:
    for parent in (binary.parent, *binary.parents):
        candidate = parent / "CMakeCache.txt"
        if candidate.is_file():
            return candidate
    return None


def build_metadata(binary: Path) -> dict[str, Any]:
    cache = find_cmake_cache(binary)
    metadata: dict[str, Any] = {"cmake_cache": str(cache) if cache else None, "build_type": "unknown", "compiler": "unknown"}
    if cache is None:
        return metadata
    values: dict[str, str] = {}
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("//") or line.startswith("#") or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        key = key_and_type.split(":", 1)[0]
        if key in ("CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER"):
            values[key] = value
    metadata["build_type"] = values.get("CMAKE_BUILD_TYPE", "unknown")
    compiler = values.get("CMAKE_CXX_COMPILER")
    if compiler:
        metadata["compiler"] = compiler
        try:
            completed = subprocess.run(
                [compiler, "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=5, check=False
            )
            metadata["compiler_version"] = completed.stdout.splitlines()[0] if completed.stdout else "unknown"
        except OSError:
            metadata["compiler_version"] = "unavailable"
    return metadata


def smoke_checks(results: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    by_id = {result["id"]: result for result in results}
    checks: list[dict[str, Any]] = []

    def maximum(result_id: str) -> float | None:
        statistics_value = by_id[result_id].get("statistics")
        return float(statistics_value["maximum"]) if statistics_value else None

    def add(name: str, passed: bool, ceiling: str) -> None:
        checks.append({"name": name, "passed": passed, "ceiling": ceiling})

    startup = maximum("warm_startup")
    add("startup_not_catastrophic", startup is not None and startup < 30_000_000_000, "< 30 seconds")
    rss = maximum("idle_rss")
    add("idle_rss_not_catastrophic", rss is None or rss < 4 * 1024 * 1024, "< 4 GiB when supported")
    growth = maximum("repeated_call_memory")
    add("repeated_memory_not_catastrophic", growth is None or growth < 512 * 1024, "< 512 MiB peak RSS growth")
    helper_wall_times = [
        float(sample["wall_time_ns"])
        for result in results
        for sample in result["samples"]
        if "wall_time_ns" in sample
    ]
    add(
        "helper_timing_not_catastrophic",
        not helper_wall_times or max(helper_wall_times) < 30_000_000_000,
        "each helper repetition < 30 seconds",
    )
    cleanup = by_id["child_cleanup"]
    cleanup_ok = cleanup["status"] == "unsupported" or (
        cleanup["status"] == "measured"
        and all(sample.get("details", {}).get("children_after") == 0 for sample in cleanup["samples"])
    )
    add("plugin_children_reaped", cleanup_ok, "zero waitable children when helper is supplied")
    manifests = by_id["unused_plugin_manifests_100"]
    manifest_ok = manifests["status"] == "unsupported" or (
        manifests["status"] == "measured"
        and all(
            sample.get("details", {}).get("children_before") == 0 and sample.get("details", {}).get("children_after") == 0
            for sample in manifests["samples"]
        )
    )
    add("manifest_discovery_starts_no_children", manifest_ok, "zero children before and after discovery")
    return checks


def validate_document(document: dict[str, Any]) -> None:
    if document.get("schema_version") != SCHEMA_VERSION:
        raise ValueError("unexpected benchmark schema version")
    required_top_level = ("generated_at_utc", "git", "host", "build", "binary", "parameters", "results", "checks")
    for key in required_top_level:
        if key not in document:
            raise ValueError(f"benchmark document lacks {key}")
    for key in ("commit", "dirty", "commit_with_state"):
        if key not in document["git"]:
            raise ValueError(f"benchmark git identity lacks {key}")
    for key in ("os", "kernel", "cpu", "ram_bytes"):
        if key not in document["host"]:
            raise ValueError(f"benchmark host identity lacks {key}")
    for key in ("build_type", "compiler"):
        if key not in document["build"]:
            raise ValueError(f"benchmark build identity lacks {key}")
    for key in ("path", "size_bytes"):
        if key not in document["binary"]:
            raise ValueError(f"benchmark binary identity lacks {key}")
    for key in ("suite", "runs", "exact_command", "clock", "environment"):
        if key not in document["parameters"]:
            raise ValueError(f"benchmark parameters lack {key}")
    results = document.get("results")
    if not isinstance(results, list):
        raise ValueError("benchmark results must be an array")
    ids = [result.get("id") for result in results]
    if ids != list(EXPECTED_RESULT_IDS):
        raise ValueError("benchmark result families are missing, duplicated, or out of order")
    for result in results:
        if not isinstance(result.get("samples"), list) or "repetitions" not in result or "statistics" not in result:
            raise ValueError(f"invalid result record: {result.get('id')}")
        if result["repetitions"] != len(result["samples"]):
            raise ValueError(f"sample count mismatch: {result['id']}")
        if result["status"] in ("measured", "current_one_shot"):
            if not result["samples"] or not isinstance(result["statistics"], dict):
                raise ValueError(f"measured result has no samples: {result['id']}")
            for key in ("median", "p95", "maximum"):
                if key not in result["statistics"]:
                    raise ValueError(f"measured result lacks {key}: {result['id']}")
        elif result["status"] != "unsupported":
            raise ValueError(f"unknown result status: {result['status']}")


def markdown_report(document: dict[str, Any]) -> str:
    lines = [
        "# AVA Backend Benchmark Report",
        "",
        f"Generated: `{document['generated_at_utc']}`  ",
        f"Suite: `{document['parameters']['suite']}` with `{document['parameters']['runs']}` measured repetition(s)  ",
        f"Git: `{document['git']['commit_with_state']}`  ",
        f"Binary: `{document['binary']['path']}` (`{document['binary']['size_bytes']}` bytes)",
        "",
        "> Results are machine/build-specific observations, not portable performance claims.",
        "",
        "| Measurement | Status | Median | p95 | Maximum |",
        "| --- | --- | ---: | ---: | ---: |",
    ]
    for result in document["results"]:
        stats = result["statistics"]
        if stats:
            unit = result["unit"]
            values = (f"{stats['median']:.3f} {unit}", f"{stats['p95']:.3f} {unit}", f"{stats['maximum']:.3f} {unit}")
        else:
            values = ("—", "—", "—")
        lines.append(f"| `{result['id']}` | {result['status']} | {values[0]} | {values[1]} | {values[2]} |")
    lines.extend(["", "## Unsupported and compatibility notes", ""])
    for result in document["results"]:
        if result["status"] == "unsupported":
            lines.append(f"- `{result['id']}`: {result['reason']}")
        elif result["status"] == "current_one_shot":
            lines.append(f"- `{result['id']}`: {result.get('note', 'Measured through the current one-shot path.')}")
    lines.extend(["", "## Smoke invariants", ""])
    for check in document["checks"]:
        lines.append(f"- {'PASS' if check['passed'] else 'FAIL'} `{check['name']}` ({check['ceiling']})")
    return "\n".join(lines) + "\n"


def execute(args: argparse.Namespace) -> dict[str, Any]:
    repository = Path(__file__).resolve().parents[1]
    root = Path(tempfile.mkdtemp(prefix="ava-backend-benchmark-"))
    project = root / "project"
    project.mkdir()
    (project / ".git").mkdir()
    results: list[dict[str, Any]] = []
    try:
        warm_startup = run_startup(args, root, project)
        results.append(
            unsupported(
                "cold_startup",
                "startup",
                "A reproducible cold start requires privileged filesystem/page-cache controls; the harness does not mislabel a fresh HOME as a cold machine.",
                "cold_cache_control_unavailable",
                "warm_startup",
            )
        )
        results.append(warm_startup)
        results.append(run_idle_memory(args, root, project))
        results.append(
            unsupported(
                "builtin_noop_dispatch",
                "dispatch",
                "The pre-modernization built-in registry has no no-op tool. The closest test-only no-op path is recorded by short_calls_10000.",
                "no_builtin_noop",
                "short_calls_10000",
            )
        )
        file_iterations = {"smoke": 10, "baseline": 1000, "stress": 10000}[args.suite]
        results.append(
            run_helper(args, root, project, "native_file_read_dispatch", "dispatch", "file-dispatch", ["--iterations", str(file_iterations)])
        )
        results.append(
            run_helper(args, root, project, "native_registry_schema_selection", "registry", "native-registry", ["--iterations", str(file_iterations)])
        )
        plugin_note = (
            "The current application path starts and shuts down a plugin process for each call; this is not persistent warm-worker performance."
        )
        results.append(
            run_helper(
                args,
                root,
                project,
                "plugin_first_call",
                "plugin",
                "plugin-call",
                ["--iterations", "1", "--sample-plugin", str(args.sample_plugin)],
                "current_one_shot",
                plugin_note,
            )
        )
        repeated_plugins = {"smoke": 2, "baseline": 10, "stress": 100}[args.suite]
        results.append(
            run_helper(
                args,
                root,
                project,
                "plugin_repeated_calls",
                "plugin",
                "plugin-call",
                ["--iterations", str(repeated_plugins), "--sample-plugin", str(args.sample_plugin)],
                "current_one_shot",
                plugin_note,
            )
        )
        results.append(
            run_helper(
                args,
                root,
                project,
                "unused_plugin_manifests_100",
                "plugin",
                "manifest-discovery",
                ["--entries", "100", "--sample-plugin", str(args.sample_plugin)],
                note="Manifest parsing only, with a zero-waitable-child assertion before and after discovery.",
            )
        )
        for count in (100, 500, 1000):
            result_id = f"catalog_entries_{count}"
            if args.suite == "smoke" and count != 100:
                results.append(not_selected(result_id, "catalog", args.suite, "catalog_entries_100"))
            else:
                results.append(
                    run_helper(
                        args,
                        root,
                        project,
                        result_id,
                        "catalog",
                        "catalog",
                        ["--entries", str(count)],
                        note="Current full schema materialization plus linear ToolRegistry selection; not a selective router.",
                    )
                )
        if args.suite == "smoke":
            results.append(not_selected("short_calls_10000", "dispatch", args.suite, "native_file_read_dispatch"))
            results.append(not_selected("canceled_calls_10000", "cancellation", args.suite, "cancellation_acknowledgement"))
        else:
            results.append(run_helper(args, root, project, "short_calls_10000", "dispatch", "short-calls", ["--iterations", "10000"]))
            results.append(run_helper(args, root, project, "canceled_calls_10000", "cancellation", "canceled-calls", ["--iterations", "10000"]))
        for records in (1000, 10000, 100000):
            result_id = f"session_open_{records}"
            if args.suite == "smoke" and records != 1000:
                results.append(not_selected(result_id, "session", args.suite, "session_open_1000"))
            else:
                results.append(
                    run_helper(args, root, project, result_id, "session", "session-open", ["--records", str(records)])
                )
        metadata_count = {"smoke": 10, "baseline": 100, "stress": 1000}[args.suite]
        results.append(
            run_helper(args, root, project, "metadata_browsing", "session", "metadata", ["--entries", str(metadata_count)])
        )
        results.append(
            run_helper(args, root, project, "cancellation_acknowledgement", "cancellation", "cancellation-ack", ["--iterations", "10"])
        )
        results.append(
            run_helper(
                args,
                root,
                project,
                "child_cleanup",
                "process",
                "plugin-cleanup",
                ["--iterations", "2", "--sample-plugin", str(args.sample_plugin)],
            )
        )
        memory_calls = 1000 if args.suite == "smoke" else 10000
        results.append(
            run_helper(args, root, project, "repeated_call_memory", "memory", "repeated-memory", ["--iterations", str(memory_calls)])
        )
        results.append(
            unsupported(
                "selective_router",
                "catalog",
                "The pre-modernization architecture materializes the full catalog and performs linear registry lookup; no selective router exists yet.",
                "architecture_not_implemented",
                "catalog_entries_1000" if args.suite != "smoke" else "catalog_entries_100",
            )
        )

        binary_stat = args.ava.stat()
        document: dict[str, Any] = {
            "schema_version": SCHEMA_VERSION,
            "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "git": git_identity(repository),
            "host": {
                "os": platform.system(),
                "platform": platform.platform(),
                "kernel": platform.release(),
                "machine": platform.machine(),
                "cpu": cpu_model(),
                "cpu_count": os.cpu_count(),
                "ram_bytes": total_ram_bytes(),
                "python": platform.python_version(),
            },
            "build": build_metadata(args.ava),
            "binary": {"path": str(args.ava), "size_bytes": binary_stat.st_size},
            "parameters": {
                "suite": args.suite,
                "runs": args.runs,
                "exact_command": list(sys.argv),
                "benchmark_helper": str(args.benchmark_helper) if args.benchmark_helper else None,
                "fake_provider": str(args.fake_provider) if args.fake_provider else None,
                "sample_plugin": str(args.sample_plugin),
                "environment": "isolated HOME/XDG/TMPDIR; credential/proxy variables removed; AVA offline",
                "clock": "time.monotonic_ns / C++ steady_clock",
            },
            "results": results,
        }
        document["checks"] = smoke_checks(results)
        validate_document(document)
        return document
    finally:
        shutil.rmtree(root, ignore_errors=True)


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        validate_arguments(args)
        document = execute(args)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if args.report is not None:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(markdown_report(document), encoding="utf-8")
        print(f"JSON: {args.output}")
        if args.report is not None:
            print(f"Markdown: {args.report}")
        failed = [check for check in document["checks"] if not check["passed"]]
        if failed:
            for check in failed:
                print(f"smoke invariant failed: {check['name']} ({check['ceiling']})", file=sys.stderr)
            return 1
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired, json.JSONDecodeError) as error:
        print(f"benchmark-backend.py: error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
