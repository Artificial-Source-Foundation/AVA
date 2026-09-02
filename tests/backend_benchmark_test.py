#!/usr/bin/env python3
"""Focused self-tests for scripts/benchmark-backend.py."""

from __future__ import annotations

import argparse
import contextlib
import copy
import hashlib
import importlib.util
import io
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


def load_module(path: pathlib.Path):
    spec = importlib.util.spec_from_file_location("ava_backend_benchmark", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BenchmarkHarnessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.module = load_module(pathlib.Path(cls.script).resolve())

    def test_statistics_use_nearest_rank_p95(self) -> None:
        summary = self.module.summarize([1.0, 2.0, 3.0, 100.0])
        self.assertEqual(summary, {"median": 2.5, "p95": 100.0, "maximum": 100.0})
        self.assertEqual(self.module.percentile_95([9.0]), 9.0)

    def test_argument_validation_requires_positive_runs_and_ava(self) -> None:
        parser = self.module.build_parser()
        with self.assertRaises(SystemExit):
            parser.parse_args(["--suite", "smoke", "--runs", "1", "--output", "out.json"])
        with self.assertRaises(SystemExit):
            parser.parse_args(["--ava", "ava", "--suite", "smoke", "--runs", "0", "--output", "out.json"])

    def parse_minimal_arguments(self, suite: str, output: pathlib.Path):
        return self.module.build_parser().parse_args(
            [
                "--ava",
                sys.executable,
                "--suite",
                suite,
                "--runs",
                "1",
                "--output",
                str(output),
            ]
        )

    def test_smoke_without_benchmark_helper_fails_before_execution(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = pathlib.Path(temporary) / "out.json"
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                status = self.module.main(
                    [
                        "--ava",
                        sys.executable,
                        "--suite",
                        "smoke",
                        "--runs",
                        "1",
                        "--output",
                        str(output),
                    ]
                )
            self.assertFalse(output.exists())
        self.assertEqual(status, 2)
        self.assertIn("--suite smoke requires an executable --benchmark-helper", stderr.getvalue())

    def test_smoke_rejects_non_executable_helper_but_baseline_may_omit_it(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            helper = root / "helper"
            helper.write_text("not executable\n", encoding="utf-8")
            smoke = self.parse_minimal_arguments("smoke", root / "smoke.json")
            smoke.benchmark_helper = helper
            with self.assertRaisesRegex(ValueError, "not executable"):
                self.module.validate_arguments(smoke)

            baseline = self.parse_minimal_arguments("baseline", root / "baseline.json")
            self.module.validate_arguments(baseline)
            self.assertIsNone(baseline.benchmark_helper)

    def test_environment_is_a_fixed_allowlist(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            home = pathlib.Path(temporary) / "home"
            inherited = {
                "AVA_SELF_TEST_KEEP": "must-not-survive",
                "OPENAI_API_KEY": "must-not-leak",
                "MY_PASSWORD": "must-not-leak",
                "HTTPS_PROXY": "http://network.invalid",
                "DATABASE_URL": "postgres://credential.invalid/db",
                "DOCKER_AUTH_CONFIG": "must-not-leak",
                "GIT_ASKPASS": "/host/askpass",
                "SSH_ASKPASS": "/host/ssh-askpass",
                "KRB5CCNAME": "/host/krb5cc",
                "LD_PRELOAD": "/host/injected.so",
                "PYTHONPATH": "/host/python",
                "ARBITRARY_PROVIDER_BASE_URL": "https://provider.invalid",
                "HOME": "/not/the/test/home",
            }
            with mock.patch.dict(os.environ, inherited, clear=True):
                environment = self.module.isolated_environment(home)

        self.assertEqual(
            environment,
            {
                "PATH": "/usr/local/bin:/usr/bin:/bin",
                "LANG": "C.UTF-8",
                "LC_ALL": "C.UTF-8",
                "HOME": str(home),
                "XDG_CONFIG_HOME": str(home / ".config"),
                "XDG_CACHE_HOME": str(home / ".cache"),
                "XDG_DATA_HOME": str(home / ".local" / "share"),
                "XDG_STATE_HOME": str(home / ".local" / "state"),
                "TMPDIR": str(home / "tmp"),
                "TERM": "dumb",
                "NO_COLOR": "1",
                "GIT_TERMINAL_PROMPT": "0",
                "AVA_BENCHMARK_OFFLINE": "1",
                "AVA_NO_DEBUG_OUTPUT": "1",
                "LIBCWD_NO_STARTUP_MSGS": "1",
                "AVA_SESSION_TITLES": "off",
            },
        )
        for name in inherited:
            if name != "HOME":
                self.assertNotIn(name, environment)

    def test_idle_memory_uses_run_maximum_and_retains_raw_snapshots(self) -> None:
        runs = [
            {
                "summary": {"rss_kib": 20.0},
                "samples": [
                    {"rss_kib": 12, "pss_kib": 7, "process_names": ["ava"]},
                    {"rss_kib": 42, "pss_kib": 8, "process_names": ["ava", "child"]},
                    {"rss_kib": 30, "pss_kib": 9, "process_names": ["ava"]},
                ],
                "captured_output_bytes": 3,
            }
        ]
        samples = self.module.idle_memory_samples(runs)
        self.assertEqual(samples[0]["value"], 42.0)
        self.assertEqual(samples[0]["details"]["rss_aggregation"], "maximum_observed_snapshot")
        self.assertEqual(samples[0]["details"]["raw_memory_helper_run"], runs[0])

    def test_run_helper_rejects_zero_runtime_samples_without_assert(self) -> None:
        args = argparse.Namespace(
            benchmark_helper=pathlib.Path(sys.executable),
            runs=0,
            suite="baseline",
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            project = root / "project"
            project.mkdir()
            with self.assertRaisesRegex(RuntimeError, "produced no measured samples"):
                self.module.run_helper(args, root, project, "result", "family", "case", [])

    def artifact(self, path: str = "/test/artifact"):
        return {
            "path": path,
            "size_bytes": 1,
            "sha256": "0" * 64,
            "mtime_ns": 1,
            "executable": True,
        }

    def valid_document(self):
        ava = self.artifact("/test/ava")
        ava["version_probe"] = {
            "status": "recorded",
            "exact_command": ["/test/ava", "--version"],
            "return_code": 0,
            "stdout": "ava test",
            "stderr": "",
        }
        return {
            "schema_version": self.module.SCHEMA_VERSION,
            "generated_at_utc": "2026-01-01T00:00:00+00:00",
            "git": {
                "repository": "/test/source",
                "commit": "a" * 40,
                "tree": "b" * 40,
                "dirty": False,
                "commit_with_state": "a" * 40,
            },
            "host": {"os": "test", "kernel": "test", "cpu": "test", "ram_bytes": 1},
            "build": {
                "cmake_cache": "/test/build/CMakeCache.txt",
                "cmake_source_root": "/test/source",
                "build_type": "Release",
                "compiler": {
                    "path": "/usr/bin/c++",
                    "id": "Test",
                    "configured_version": "1.0",
                    "version_output": "Test 1.0",
                },
                "features": {"sanitizers": False, "tsan": False, "debug": False, "libcwd": False},
                "cmake_flags": {},
                "cxx_flags": {},
                "provenance": {
                    "assessment": "best_effort_unverified",
                    "statement": "No commit embedding claim.",
                    "cmake_source_root_matches_recorded_source": True,
                    "binary_is_within_cmake_build_tree": True,
                    "binary_mtime_ns": 2,
                    "cmake_cache_mtime_ns": 1,
                    "binary_not_older_than_cmake_cache": True,
                    "git_commit_embedding_verified": False,
                },
            },
            "artifacts": {
                "ava": ava,
                "benchmark_helper": self.artifact("/test/helper"),
                "fake_provider": self.artifact("/test/fake-provider"),
                "memory_helper": self.artifact("/test/memory.py"),
                "sample_plugin": {
                    "root": "/test/plugin",
                    "manifest": self.artifact("/test/plugin/plugin.json"),
                    "entrypoint": self.artifact("/test/plugin/plugin.sh"),
                    "manifest_identity": {
                        "schema_version": 1,
                        "id": "test.plugin",
                        "name": "Test",
                        "version": "1.0",
                        "api_version": "test.v1",
                        "entrypoint_command": "/bin/sh",
                        "entrypoint_args": ["plugin.sh"],
                    },
                },
                "benchmark_script": self.artifact("/test/benchmark.py"),
            },
            "parameters": {
                "suite": "smoke",
                "runs": 1,
                "exact_command": ["benchmark"],
                "clock": "monotonic",
                "environment": "fixed allowlist",
            },
            "results": [
                self.module.unsupported(result_id, "self-test", "not measured", "self_test")
                for result_id in self.module.EXPECTED_RESULT_IDS
            ],
            "checks": [],
        }

    def test_file_identity_hashes_bytes_with_python_stdlib(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = pathlib.Path(temporary) / "artifact"
            path.write_bytes(b"abc")
            identity = self.module.file_identity(path)
        self.assertEqual(identity["size_bytes"], 3)
        self.assertEqual(identity["sha256"], hashlib.sha256(b"abc").hexdigest())

    def test_schema_requires_every_family_and_source_build_identity(self) -> None:
        document = self.valid_document()
        self.module.validate_document(document)

        document["results"] = document["results"][:-1]
        with self.assertRaisesRegex(ValueError, "missing"):
            self.module.validate_document(document)

        document = self.valid_document()
        del document["host"]
        with self.assertRaisesRegex(ValueError, "lacks host"):
            self.module.validate_document(document)

        document = self.valid_document()
        del document["git"]["tree"]
        with self.assertRaisesRegex(ValueError, "git identity lacks tree"):
            self.module.validate_document(document)

        document = self.valid_document()
        del document["build"]["cmake_source_root"]
        with self.assertRaisesRegex(ValueError, "build identity lacks cmake_source_root"):
            self.module.validate_document(document)

    def test_schema_requires_artifact_hashes_and_plugin_entrypoint_identity(self) -> None:
        document = self.valid_document()
        del document["artifacts"]["memory_helper"]["sha256"]
        with self.assertRaisesRegex(ValueError, "memory_helper lacks sha256"):
            self.module.validate_document(document)

        document = self.valid_document()
        del document["artifacts"]["sample_plugin"]["entrypoint"]
        with self.assertRaisesRegex(ValueError, "sample plugin identity lacks entrypoint"):
            self.module.validate_document(document)

        document = self.valid_document()
        document["artifacts"]["benchmark_helper"] = None
        with self.assertRaisesRegex(ValueError, "smoke benchmark artifact identity lacks benchmark_helper"):
            self.module.validate_document(document)

    def test_schema_requires_real_native_registry_lookup_identity(self) -> None:
        document = self.valid_document()
        index = self.module.EXPECTED_RESULT_IDS.index("native_registry_lookup")
        document["results"][index] = self.module.measured_result(
            "native_registry_lookup",
            "registry",
            "measured",
            "ns_per_lookup",
            [{"value": 1.0, "details": {"target": "question", "entry_count": 20}}],
            ["helper"],
        )
        self.module.validate_document(document)
        document["results"][index]["unit"] = "invalid_unit"
        with self.assertRaisesRegex(ValueError, "invalid unit"):
            self.module.validate_document(document)

    def test_measured_schema_rejects_missing_statistics(self) -> None:
        document = self.valid_document()
        document["results"][0] = {
            "id": self.module.EXPECTED_RESULT_IDS[0],
            "family": "self-test",
            "status": "measured",
            "unit": "ns",
            "repetitions": 1,
            "samples": [{"value": 1.0}],
            "statistics": None,
        }
        with self.assertRaisesRegex(ValueError, "no samples"):
            self.module.validate_document(document)

    def test_required_smoke_seams_fail_when_unsupported(self) -> None:
        results = [
            self.module.unsupported(result_id, "self-test", "not measured", "self_test")
            for result_id in self.module.EXPECTED_RESULT_IDS
        ]
        checks = {check["name"]: check["passed"] for check in self.module.smoke_checks(results)}
        for name in (
            "native_dispatch_exercised",
            "cancellation_acknowledgement_exercised",
            "session_open_exercised",
            "repeated_memory_exercised",
            "plugin_children_reaped",
            "manifest_discovery_starts_no_children",
        ):
            self.assertFalse(checks[name], name)

    def process_artifact(self, path: str) -> dict[str, object]:
        artifact = self.artifact(path)
        artifact["mode"] = 0o755
        return artifact

    def process_sample(self, value: float = 1.0, checks: dict[str, object] | None = None) -> dict[str, object]:
        return {
            "run": 1,
            "observation": 1,
            "value": value,
            "metrics": {"helper_invocation_ns": 10.0},
            "checks": checks or {"correct": True},
        }

    def process_family_sources(self):
        identities = {}
        for family, (tree_path, blob_paths) in self.module.PROCESS_FAMILY_SOURCE_PATHS.items():
            identities[family] = {
                "tree_path": tree_path,
                "tree_object": hashlib.sha256(f"tree:{family}".encode()).hexdigest()[:40],
                "blobs": [
                    {"path": path, "object": hashlib.sha256(f"blob:{path}".encode()).hexdigest()[:40]}
                    for path in blob_paths
                ],
            }
        return identities

    def valid_process_document(self):
        artifacts = {
            "ava": self.process_artifact("/test/build/ava"),
            "benchmark_helper": self.process_artifact("/test/build/tests/helper"),
            "benchmark_script": self.process_artifact("/test/harness/scripts/benchmark-backend.py"),
            "memory_helper": self.process_artifact("/test/memory.py"),
            "python": self.process_artifact("/test/python"),
            "fake_process_child": self.process_artifact("/test/process-child"),
            "fake_mcp_server": self.process_artifact("/test/mcp"),
            "fake_lsp_server": self.process_artifact("/test/lsp"),
            "fake_provider": None,
            "curl": self.process_artifact("/test/curl"),
            "bash_direct_argv_executable": self.process_artifact("/test/pwd"),
            "sample_plugin": {
                "root": "/test/plugin",
                "manifest": self.process_artifact("/test/plugin/plugin.json"),
                "entrypoint": self.process_artifact("/test/plugin/plugin.sh"),
            },
        }
        host = {
            "os": "TestOS",
            "platform": "TestOS",
            "kernel": "1",
            "machine": "test",
            "cpu": "test cpu",
            "cpu_count": 1,
            "ram_bytes": 1,
            "page_size_bytes": 4096,
            "python_version": "3.test",
            "python_implementation": "CPython",
            "boot_id_sha256": "a" * 64,
            "limits": {},
            "monotonic_clock_resolution_ns": 1.0,
            "load_at_start": [0.0, 0.0, 0.0],
            "load_at_end": [0.0, 0.0, 0.0],
        }
        recipe = {"generator": "Ninja", "cmake_version": "cmake 1", "build_type": "Release", "cmake_flags": {}, "cxx_flags": {}}
        authorities = {name: "legacy_local" for name in self.module.PROCESS_FAMILY_NAMES}
        compiler_artifact = self.process_artifact("/usr/bin/c++")
        build = {
            "generator": "Ninja",
            "cmake_version": "cmake 1",
            "cmake": self.process_artifact("/usr/bin/cmake"),
            "cmake_cache": self.process_artifact("/test/build/CMakeCache.txt"),
            "cmake_source_root": "/test/source",
            "build_type": "Release",
            "features": {
                "sanitizers": False,
                "tsan": False,
                "debug": False,
                "libcwd": False,
                "process_supervisor": True,
                "process_fixture": True,
                "platform_backend": "posix",
                "family_authorities": dict(authorities),
            },
            "compiler": {
                "path": "/usr/bin/c++",
                "artifact": compiler_artifact,
                "id": "Test",
                "configured_version": "1",
                "version_output": "Test 1",
                "flags": {},
            },
            "recipe": recipe,
            "best_effort_provenance": {
                "assessment": "best_effort_unverified",
                "statement": "No embedded commit claim.",
                "git_commit_embedding_verified": False,
                "cmake_source_root_matches_recorded_source": True,
                "binary_is_within_cmake_build_tree": True,
                "binary_not_older_than_cmake_cache": True,
            },
        }
        helper_build = copy.deepcopy(build)
        build_binding = self.module.process_binary_build_binding(build, helper_build)
        results = [
            self.module.process_unsupported_result(result_id, "fixture_unavailable")
            for result_id in self.module.PROCESS_EXPECTED_RESULT_IDS
        ]
        return {
            "schema_version": self.module.PROCESS_SCHEMA_VERSION,
            "contract_version": self.module.PROCESS_CONTRACT_VERSION,
            "generated_at_utc": "2026-01-01T00:00:00+00:00",
            "completed_at_utc": "2026-01-01T00:00:01+00:00",
            "suite": "process-baseline",
            "provenance": {
                "measured_checkout": {"repository": "/test/source", "commit": "1" * 40, "tree": "2" * 40, "dirty": False},
                "runtime_reference": {
                    "commit": "1" * 40,
                    "tree": "2" * 40,
                    "exact_production_path_equality": True,
                    "measured_production_paths_dirty": False,
                },
                "harness": {
                    "repository": "/test/harness",
                    "commit": "3" * 40,
                    "tree": "4" * 40,
                    "dirty": False,
                    "benchmark_script_sha256": "0" * 64,
                    "contract_version": self.module.PROCESS_CONTRACT_VERSION,
                },
                "family_sources": self.process_family_sources(),
                "family_authorities": {
                    "path": self.module.PROCESS_AUTHORITY_SOURCE_PATH,
                    "object": "5" * 40,
                    "sha256": "6" * 64,
                    "authorities": dict(authorities),
                },
                "build": build,
                "benchmark_helper_build": helper_build,
                "binary_build_binding": build_binding,
                "host": host,
                "driver": {"exact_command": ["benchmark"], "run_order": list(self.module.PROCESS_EXPECTED_RESULT_IDS)},
            },
            "artifacts": artifacts,
            "capabilities": {
                "helper_contract": self.module.PROCESS_CONTRACT_VERSION,
                **{f"{name}_authority": "legacy_local" for name in ("curl", "plugin", "mcp", "lsp", "bash")},
            },
            "results": results,
            "checks": [],
        }

    def measured_process_result(self, result_id: str, authority: str = "legacy_local", value: float = 1.0):
        checks: dict[str, object] = {
            "protocol_compatible": True,
            "expected_response": True,
            "shutdown_complete": True,
            "immediate_child_guard": True,
        }
        if authority == "supervised":
            checks.update({"supervisor_record_finished": True, "supervisor_settlement_once": True})
        metadata = {"authority": authority} if result_id.startswith("family_") else None
        return self.module.process_measured_result(result_id, [self.process_sample(value, checks)], 1, metadata)

    def test_process_schema_preserves_order_raw_correlation_and_metric_statistics(self) -> None:
        document = self.valid_process_document()
        result_id = "supervisor_warm_sequential_spawn_commit"
        index = self.module.PROCESS_EXPECTED_RESULT_IDS.index(result_id)
        samples = [
            {"run": 1, "observation": ordinal, "value": value, "metrics": {"batch_spawn_commit_ns": value + 1}, "checks": {"correct": True}}
            for ordinal, value in enumerate((1.0, 2.0, 3.0, 100.0), 1)
        ]
        document["results"][index] = self.module.process_measured_result(result_id, samples, 1)
        self.module.validate_process_document(document)
        result = document["results"][index]
        self.assertEqual(result["statistics"]["primary"], {"median": 2.5, "p95": 100.0, "maximum": 100.0})
        self.assertEqual(result["observation_count"], 4)

        document["results"][index], document["results"][index + 1] = document["results"][index + 1], document["results"][index]
        with self.assertRaisesRegex(ValueError, "out of order"):
            self.module.validate_process_document(document)

    def test_process_schema_rejects_redaction_canaries_and_false_supervised_claims(self) -> None:
        document = self.valid_process_document()
        result_id = "family_plugin_lifecycle"
        index = self.module.PROCESS_EXPECTED_RESULT_IDS.index(result_id)
        document["results"][index] = self.measured_process_result(result_id)
        document["results"][index]["samples"][0]["checks"]["tool_content"] = "CANARY_REDACTION"
        with self.assertRaisesRegex(ValueError, "prohibited"):
            self.module.validate_process_document(document)

        document = self.valid_process_document()
        result = self.measured_process_result(result_id, authority="legacy_local")
        result["authority"] = "supervised"
        result["metadata"]["authority"] = "supervised"
        document["results"][index] = result
        with self.assertRaisesRegex(ValueError, "false supervised claim"):
            self.module.validate_process_document(document)

    def test_process_schema_rejects_composite_redaction_keys_and_negative_samples(self) -> None:
        result_id = "supervisor_first_spawn_commit"
        index = self.module.PROCESS_EXPECTED_RESULT_IDS.index(result_id)
        prohibited_keys = (
            "child_pid",
            "raw_pgid",
            "request_url",
            "exec_path",
            "command_argv",
            "ownerRawId",
            "endpoint-fd",
            "child_output_bytes",
            "protocol.frame",
            "environmentValue",
            "toolContent",
            "prompt_bytes",
            "processExecutable",
        )
        for key in prohibited_keys:
            with self.subTest(key=key):
                document = self.valid_process_document()
                result = self.measured_process_result(result_id)
                result["samples"][0]["metrics"][key] = 1
                result["statistics"] = self.module.process_statistics(result["samples"])
                document["results"][index] = result
                with self.assertRaisesRegex(ValueError, "prohibited"):
                    self.module.validate_process_document(document)

        document = self.valid_process_document()
        result = self.measured_process_result(result_id)
        result["samples"][0]["metrics"].update(
            {"pidfd_successes": 1, "stdout_bytes": 2, "record_count": 3, "endpoint_eof": True}
        )
        result["statistics"] = self.module.process_statistics(result["samples"])
        document["results"][index] = result
        self.module.validate_process_document(document)

        document = self.valid_process_document()
        document["results"][index] = self.measured_process_result(result_id, value=-1.0)
        with self.assertRaisesRegex(ValueError, "non-negative"):
            self.module.validate_process_document(document)

    def test_helper_v2_preserves_observations_and_accepts_static_unsupported(self) -> None:
        measured = {
            "helper_schema_version": self.module.PROCESS_HELPER_SCHEMA_VERSION,
            "case": "process-first-spawn",
            "status": "measured",
            "primary_metric": "spawn_commit_ns",
            "unit": "ns",
            "observations": [
                {"ordinal": 1, "value": 1, "metrics": {}, "checks": {"confirmed_exec": True}},
                {"ordinal": 2, "value": 2, "metrics": {}, "checks": {"confirmed_exec": True}},
            ],
            "case_metrics": {"authority": "neutral_supervisor"},
        }
        self.assertEqual(len(self.module.validate_helper_payload(measured, "process-first-spawn")["observations"]), 2)
        measured["observations"][0]["metrics"]["child_pid"] = 123
        with self.assertRaisesRegex(RuntimeError, "prohibited"):
            self.module.validate_helper_payload(measured, "process-first-spawn")
        del measured["observations"][0]["metrics"]["child_pid"]

        unsupported = {
            "helper_schema_version": self.module.PROCESS_HELPER_SCHEMA_VERSION,
            "case": "process-first-spawn",
            "status": "unsupported",
            "primary_metric": "spawn_commit_ns",
            "unit": "ns",
            "reason_code": "source_architecture_absent",
            "reason": self.module.PROCESS_REASON_TEXT["source_architecture_absent"],
            "observations": [],
            "case_metrics": {"authority": "neutral_supervisor"},
        }
        self.module.validate_helper_payload(unsupported, "process-first-spawn")
        unsupported["reason"] = "dynamic detail"
        with self.assertRaisesRegex(RuntimeError, "unsupported reason"):
            self.module.validate_helper_payload(unsupported, "process-first-spawn")

    def test_process_helper_preserves_plugin_caller_not_migrated_unsupported(self) -> None:
        args = argparse.Namespace(
            benchmark_helper=pathlib.Path(sys.executable),
            fake_process_child=None,
            fake_mcp_server=None,
            fake_lsp_server=None,
            sample_plugin=pathlib.Path("/fixture"),
            runs=1,
        )
        payload = {
            "helper_schema_version": self.module.PROCESS_HELPER_SCHEMA_VERSION,
            "case": "family-plugin-lifecycle",
            "status": "unsupported",
            "primary_metric": "lifecycle_ns",
            "unit": "ns",
            "reason_code": "caller_not_migrated",
            "reason": self.module.PROCESS_REASON_TEXT["caller_not_migrated"],
            "observations": [],
            "case_metrics": {"authority": "legacy_local", "cleanup_scope": "immediate_children_only"},
        }
        completed = self.module.subprocess.CompletedProcess([], 0, self.module.json.dumps(payload), "")
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            project = root / "project"
            project.mkdir()
            commands: list[dict[str, object]] = []
            with mock.patch.object(self.module, "run_process", return_value=(1.0, completed)):
                result = self.module.run_process_helper(args, root, project, "family_plugin_lifecycle", [], commands)
        self.assertEqual(result["status"], "unsupported")
        self.assertEqual(result["reason_code"], "caller_not_migrated")
        self.assertEqual(len(commands), 1)

    def test_process_helper_rejects_malformed_and_truncated_output(self) -> None:
        args = argparse.Namespace(
            benchmark_helper=pathlib.Path(sys.executable),
            fake_process_child=None,
            fake_mcp_server=None,
            fake_lsp_server=None,
            sample_plugin=pathlib.Path("/fixture"),
            runs=1,
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            project = root / "project"
            project.mkdir()
            for output in ("{", "{} trailing"):
                completed = self.module.subprocess.CompletedProcess([], 0, output, "")
                with mock.patch.object(self.module, "run_process", return_value=(1.0, completed)):
                    with self.assertRaisesRegex(RuntimeError, "malformed or truncated"):
                        self.module.run_process_helper(args, root, project, "supervisor_first_spawn_commit", [], [])

    def test_process_argument_validation_rejects_missing_and_nonexecuting_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            output = root / "out.json"
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                status = self.module.main(
                    ["--ava", sys.executable, "--suite", "process-smoke", "--runs", "1", "--output", str(output)]
                )
            self.assertEqual(status, 2)
            self.assertIn("requires an executable --benchmark-helper", stderr.getvalue())

            fixture = root / "fixture"
            fixture.write_text("not executable", encoding="utf-8")
            args = self.module.build_parser().parse_args(
                [
                    "--ava", sys.executable,
                    "--benchmark-helper", sys.executable,
                    "--fake-process-child", str(fixture),
                    "--suite", "process-smoke",
                    "--runs", "1",
                    "--output", str(output),
                ]
            )
            with self.assertRaisesRegex(ValueError, "not executable"):
                self.module.validate_arguments(args)

    def test_measured_source_root_validation_is_process_only_and_precedes_execution(self) -> None:
        repository = pathlib.Path(self.script).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            output = root / "out.json"

            non_process = self.parse_minimal_arguments("baseline", output)
            non_process.measured_source_root = repository
            with self.assertRaisesRegex(ValueError, "only with process suites"):
                self.module.validate_arguments(non_process)

            invalid_roots = [root / "missing", root / "incomplete", root / "not-git"]
            (root / "incomplete").mkdir()
            non_git = root / "not-git"
            (non_git / "src").mkdir(parents=True)
            (non_git / "cmake").mkdir()
            (non_git / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.20)\n", encoding="utf-8")
            (non_git / "config.h.in").write_text("\n", encoding="utf-8")
            for invalid_root in invalid_roots:
                with self.subTest(root=invalid_root.name):
                    argv = [
                        "--ava", sys.executable,
                        "--benchmark-helper", sys.executable,
                        "--measured-source-root", str(invalid_root),
                        "--suite", "process-smoke",
                        "--runs", "1",
                        "--output", str(output),
                    ]
                    with mock.patch.object(self.module, "execute_process") as execute_process:
                        status = self.module.main(argv)
                    self.assertEqual(status, 2)
                    self.assertFalse(output.exists())
                    execute_process.assert_not_called()

    def test_default_process_source_root_retains_script_repository_identity(self) -> None:
        repository = pathlib.Path(self.script).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temporary:
            args = self.module.build_parser().parse_args(
                [
                    "--ava", sys.executable,
                    "--benchmark-helper", sys.executable,
                    "--suite", "process-smoke",
                    "--runs", "1",
                    "--output", str(pathlib.Path(temporary) / "out.json"),
                ]
            )
            self.module.validate_arguments(args)
        self.assertEqual(args.measured_source_root, repository)
        provenance = self.module.process_source_provenance(args.measured_source_root, repository, "HEAD")
        self.assertEqual(provenance["measured_checkout"]["repository"], str(repository))
        self.assertEqual(provenance["harness"]["repository"], str(repository))
        self.assertEqual(provenance["measured_checkout"]["commit"], provenance["harness"]["commit"])

        non_process = self.parse_minimal_arguments("baseline", pathlib.Path("default-identities.json"))
        self.module.validate_arguments(non_process)
        self.assertIsNone(non_process.measured_source_root)

    def test_git_status_failures_remain_unresolved_not_false_clean(self) -> None:
        def git_identity_run(command, **_kwargs):
            if "status" in command:
                return subprocess.CompletedProcess(command, 2, "", "status failed")
            value = "a" * 40 if command[-1] == "HEAD" else "b" * 40
            return subprocess.CompletedProcess(command, 0, value + "\n", "")

        with mock.patch.object(self.module.subprocess, "run", side_effect=git_identity_run):
            identity = self.module.git_identity(pathlib.Path("/test/repository"))
        self.assertIsNone(identity["dirty"])
        self.assertEqual(identity["commit_with_state"], f"{'a' * 40}-state-unknown")

        repository = pathlib.Path(self.script).resolve().parents[1]
        clean_identity = {
            "repository": str(repository),
            "commit": "a" * 40,
            "tree": "b" * 40,
            "dirty": False,
            "commit_with_state": "a" * 40,
        }

        def provenance_git(_repository, *arguments):
            if arguments[0] == "status":
                return "unknown"
            return "a" * 40 if arguments[-1].endswith("^{commit}") else "b" * 40

        with (
            mock.patch.object(self.module, "git_identity", return_value=clean_identity),
            mock.patch.object(self.module, "_git", side_effect=provenance_git),
            mock.patch.object(
                self.module.subprocess,
                "run",
                return_value=subprocess.CompletedProcess([], 0, "", ""),
            ),
        ):
            provenance = self.module.process_source_provenance(repository, repository, "HEAD")
        self.assertIsNone(provenance["runtime_reference"]["measured_production_paths_dirty"])

    def test_explicit_measured_source_root_is_retained_in_exact_command_and_build_match(self) -> None:
        repository = pathlib.Path(self.script).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            output = root / "out.json"
            argv = [
                "--ava", sys.executable,
                "--benchmark-helper", sys.executable,
                "--measured-source-root", str(repository),
                "--suite", "process-smoke",
                "--runs", "1",
                "--output", str(output),
            ]
            with mock.patch.object(self.module, "execute_process", return_value=self.valid_process_document()) as execute_process:
                status = self.module.main(argv)
            self.assertEqual(status, 0)
            executed_args = execute_process.call_args.args[0]
            option_index = executed_args._exact_command.index("--measured-source-root")
            self.assertEqual(executed_args._exact_command[option_index + 1], str(repository))

            build = root / "build"
            build.mkdir()
            binary = build / "ava"
            binary.write_bytes(b"binary")
            (build / "CMakeCache.txt").write_text(
                f"CMAKE_HOME_DIRECTORY:INTERNAL={repository}\nCMAKE_BUILD_TYPE:STRING=Release\n",
                encoding="utf-8",
            )
            matching = self.module.build_metadata(binary, repository)
            other_root = root / "other"
            other_root.mkdir()
            nonmatching = self.module.build_metadata(binary, other_root)
            self.assertIs(matching["provenance"]["cmake_source_root_matches_recorded_source"], True)
            self.assertIs(nonmatching["provenance"]["cmake_source_root_matches_recorded_source"], False)

    def test_distinct_measured_worktrees_share_one_content_correct_harness(self) -> None:
        harness_repository = pathlib.Path(self.script).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            repository = root / "repository"
            repository.mkdir()

            def git(*arguments: str) -> str:
                completed = subprocess.run(
                    ["git", "-C", str(repository), *arguments],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    check=True,
                )
                return completed.stdout.strip()

            git("init")
            git("config", "user.name", "Benchmark Self Test")
            git("config", "user.email", "benchmark@example.invalid")
            production_files = (
                "src/ava/http/curl_transport.cpp",
                "src/ava/http/curl_transport.h",
                "src/ava/plugin/runner.cpp",
                "src/ava/plugin/runner.h",
                "src/ava/mcp/stdio_client.cpp",
                "src/ava/mcp/stdio_client.h",
                "src/ava/lsp/lsp_process.cpp",
                "src/ava/lsp/lsp_client.h",
                "src/ava/tools/bash_tool.cpp",
                "src/ava/tools/bash_tool.h",
                "CMakeLists.txt",
                "cmake/fixture.cmake",
                "config.h.in",
            )
            for relative in production_files:
                path = repository / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(f"before:{relative}\n", encoding="utf-8")
            git("add", ".")
            git("commit", "-m", "before")
            before_commit = git("rev-parse", "HEAD")
            before_tree = git("rev-parse", "HEAD^{tree}")
            (repository / "src/ava/plugin/runner_io.cpp").write_text("new plugin process seam\n", encoding="utf-8")
            git("add", "src/ava/plugin/runner_io.cpp")
            git("commit", "-m", "after")
            after_commit = git("rev-parse", "HEAD")
            after_tree = git("rev-parse", "HEAD^{tree}")

            before_root = root / "before"
            after_root = root / "after"
            git("worktree", "add", "--detach", str(before_root), before_commit)
            git("worktree", "add", "--detach", str(after_root), after_commit)
            before_root = self.module.resolve_measured_source_root(before_root)
            after_root = self.module.resolve_measured_source_root(after_root)

            before_provenance = self.module.process_source_provenance(before_root, harness_repository, "HEAD")
            after_provenance = self.module.process_source_provenance(after_root, harness_repository, "HEAD")
            before_provenance["family_sources"] = self.module.family_source_identities(before_root)
            after_provenance["family_sources"] = self.module.family_source_identities(after_root)

            self.assertEqual(before_provenance["measured_checkout"]["repository"], str(before_root))
            self.assertEqual(before_provenance["measured_checkout"]["commit"], before_commit)
            self.assertEqual(before_provenance["measured_checkout"]["tree"], before_tree)
            self.assertEqual(after_provenance["measured_checkout"]["repository"], str(after_root))
            self.assertEqual(after_provenance["measured_checkout"]["commit"], after_commit)
            self.assertEqual(after_provenance["measured_checkout"]["tree"], after_tree)
            self.assertEqual(
                before_provenance["family_sources"]["plugin"]["blobs"],
                after_provenance["family_sources"]["plugin"]["blobs"],
            )
            self.assertNotEqual(
                before_provenance["family_sources"]["plugin"]["tree_object"],
                after_provenance["family_sources"]["plugin"]["tree_object"],
            )
            self.assertEqual(before_provenance["harness"], after_provenance["harness"])

            before, after = self.comparison_documents()
            for document, measured in ((before, before_provenance), (after, after_provenance)):
                document["provenance"]["measured_checkout"] = measured["measured_checkout"]
                document["provenance"]["runtime_reference"] = measured["runtime_reference"]
                document["provenance"]["harness"] = measured["harness"]
                document["provenance"]["harness"]["dirty"] = False
                document["provenance"]["family_sources"] = measured["family_sources"]
                for build_key in ("build", "benchmark_helper_build"):
                    document["provenance"][build_key]["cmake_source_root"] = measured["measured_checkout"]["repository"]
                document["provenance"]["binary_build_binding"] = self.module.process_binary_build_binding(
                    document["provenance"]["build"], document["provenance"]["benchmark_helper_build"]
                )
                document["artifacts"]["benchmark_script"] = self.module.file_identity_v3(
                    harness_repository / "scripts" / "benchmark-backend.py"
                )
            self.assertEqual(
                before["artifacts"]["benchmark_script"]["sha256"],
                after["artifacts"]["benchmark_script"]["sha256"],
            )
            comparison = self.module.compare_process_documents(before, after)
            self.assertEqual(comparison["status"], "measured")
            self.assertNotIn("fixture_hashes", comparison.get("mismatches", []))

    def test_force_removes_linked_worktree_with_initialized_submodule_without_deinit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            submodule = root / "submodule"
            repository = root / "repository"
            linked = root / "linked"
            submodule.mkdir()
            repository.mkdir()

            def run_git(directory: pathlib.Path, *arguments: str) -> str:
                completed = subprocess.run(
                    ["git", "-C", str(directory), *arguments],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    check=True,
                )
                return completed.stdout.strip()

            for directory in (submodule, repository):
                run_git(directory, "init")
                run_git(directory, "config", "user.name", "Benchmark Self Test")
                run_git(directory, "config", "user.email", "benchmark@example.invalid")
            (submodule / "fixture.txt").write_text("fixture\n", encoding="utf-8")
            run_git(submodule, "add", "fixture.txt")
            run_git(submodule, "commit", "-m", "fixture")
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(repository),
                    "-c",
                    "protocol.file.allow=always",
                    "submodule",
                    "add",
                    str(submodule),
                    "vendor/fixture",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
            )
            run_git(repository, "commit", "-am", "superproject")
            config_before = run_git(repository, "config", "--local", "--get-regexp", r"^submodule\.")

            run_git(repository, "worktree", "add", "--detach", str(linked), "HEAD")
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(linked),
                    "-c",
                    "protocol.file.allow=always",
                    "submodule",
                    "update",
                    "--init",
                    "--recursive",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
            )
            self.assertTrue((linked / "vendor/fixture/fixture.txt").is_file())
            self.assertEqual(run_git(linked, "status", "--porcelain", "--untracked-files=normal"), "")

            run_git(repository, "worktree", "remove", "--force", str(linked))
            self.assertFalse(linked.exists())
            self.assertEqual(
                run_git(repository, "config", "--local", "--get-regexp", r"^submodule\."),
                config_before,
            )
            self.assertTrue((repository / "vendor/fixture/fixture.txt").is_file())

    def test_process_schema_requires_independent_content_correct_harness_identity(self) -> None:
        document = self.valid_process_document()
        self.module.validate_process_document(document)

        del document["provenance"]["harness"]["tree"]
        with self.assertRaisesRegex(ValueError, "harness provenance lacks tree"):
            self.module.validate_process_document(document)

        document = self.valid_process_document()
        document["provenance"]["harness"]["benchmark_script_sha256"] = "f" * 64
        with self.assertRaisesRegex(ValueError, "does not match harness content"):
            self.module.validate_process_document(document)

        document = self.valid_process_document()
        document["artifacts"]["benchmark_script"]["path"] = "/test/source/scripts/benchmark-backend.py"
        with self.assertRaisesRegex(ValueError, "not from the harness repository"):
            self.module.validate_process_document(document)

    def test_process_schema_requires_provenance_split_fields_atomically(self) -> None:
        for section, field in self.module.PROCESS_PROVENANCE_SPLIT_FIELDS:
            with self.subTest(field=f"{section}.{field}"):
                document = self.valid_process_document()
                del document["provenance"][section][field]
                with self.assertRaisesRegex(ValueError, "split fields must be present atomically"):
                    self.module.validate_process_document(document)

    def test_historical_v3_without_helper_build_binding_still_validates(self) -> None:
        document = self.valid_process_document()
        for field in ("family_authorities", "benchmark_helper_build", "binary_build_binding"):
            del document["provenance"][field]
        self.module.validate_process_document(document)

    def test_historical_v3_without_provenance_split_validates_but_cannot_compare(self) -> None:
        for measured_repository_present in (False, True):
            with self.subTest(measured_repository_present=measured_repository_present):
                before, after = self.comparison_documents()
                del before["provenance"]["harness"]["repository"]
                del before["provenance"]["harness"]["benchmark_script_sha256"]
                if not measured_repository_present:
                    del before["provenance"]["measured_checkout"]["repository"]

                self.module.validate_process_document(before)
                comparison = self.module.compare_process_documents(before, after)

                self.assertEqual(comparison["status"], "unsupported")
                self.assertEqual(comparison["reason_code"], "provenance_split_required")
                self.assertEqual(comparison["mismatches"], ["before.provenance_split"])

    def test_standalone_process_document_allows_dirty_source_and_harness(self) -> None:
        document = self.valid_process_document()
        document["provenance"]["measured_checkout"]["dirty"] = True
        document["provenance"]["runtime_reference"]["measured_production_paths_dirty"] = True
        document["provenance"]["harness"]["dirty"] = True
        self.module.validate_process_document(document)

    def test_process_smoke_threshold_failure_is_gating_but_not_a_delta_gate(self) -> None:
        document = self.valid_process_document()
        startup_index = self.module.PROCESS_EXPECTED_RESULT_IDS.index("application_warm_startup")
        rss_index = self.module.PROCESS_EXPECTED_RESULT_IDS.index("application_idle_rss")
        document["results"][startup_index] = self.measured_process_result("application_warm_startup", value=31_000_000_000.0)
        document["results"][rss_index] = self.measured_process_result("application_idle_rss", value=1.0)
        capabilities = {"process_supervisor": False, "platform_backend": "unsupported"}
        checks = {check["name"]: check["passed"] for check in self.module.process_smoke_checks(document["results"], capabilities)}
        self.assertFalse(checks["application_startup_not_catastrophic"])

    def comparison_documents(self):
        before = self.valid_process_document()
        after = self.valid_process_document()
        before_authorities = {
            "curl": "supervised",
            "plugin": "legacy_local",
            "mcp": "legacy_local",
            "lsp": "legacy_local",
            "bash": "legacy_local",
        }
        after_authorities = {**before_authorities, "plugin": "supervised"}
        for document, authorities, value in (
            (before, before_authorities, 100.0),
            (after, after_authorities, 130.0),
        ):
            provenance = document["provenance"]
            provenance["family_authorities"]["authorities"] = dict(authorities)
            provenance["build"]["features"]["family_authorities"] = dict(authorities)
            provenance["benchmark_helper_build"]["features"]["family_authorities"] = dict(authorities)
            provenance["binary_build_binding"] = self.module.process_binary_build_binding(
                provenance["build"], provenance["benchmark_helper_build"]
            )
            document["capabilities"].update(
                {f"{family}_authority": authority for family, authority in authorities.items()}
            )
            for family, authority in authorities.items():
                result_id = f"family_{family}_lifecycle"
                index = self.module.PROCESS_EXPECTED_RESULT_IDS.index(result_id)
                document["results"][index] = self.measured_process_result(result_id, authority, value)
        after["provenance"]["family_authorities"]["object"] = "7" * 40
        after["provenance"]["family_authorities"]["sha256"] = "8" * 64
        after_plugin = after["provenance"]["family_sources"]["plugin"]
        after_plugin["tree_object"] = "e" * 40
        after_plugin["blobs"][0]["object"] = "f" * 40
        return before, after

    def assert_comparison_provenance_rejected(self, before, after, mismatches) -> None:
        self.module.validate_process_document(before)
        self.module.validate_process_document(after)
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "unsupported")
        self.assertEqual(comparison["reason_code"], "comparison_provenance_required")
        self.assertEqual(comparison["mismatches"], mismatches)
        self.assertEqual(comparison["comparisons"], [])

    def test_comparison_rejects_dirty_measured_source(self) -> None:
        before, after = self.comparison_documents()
        before["provenance"]["measured_checkout"]["dirty"] = True
        self.assert_comparison_provenance_rejected(
            before,
            after,
            ["before.measured_checkout.dirty"],
        )

    def test_comparison_rejects_unresolved_measured_identity(self) -> None:
        before, after = self.comparison_documents()
        measured = before["provenance"]["measured_checkout"]
        measured.update({"repository": "unknown", "commit": "unknown", "tree": ""})
        self.assert_comparison_provenance_rejected(
            before,
            after,
            [
                "before.measured_checkout.repository",
                "before.measured_checkout.commit",
                "before.measured_checkout.tree",
            ],
        )

    def test_comparison_rejects_bad_runtime_reference_and_equality(self) -> None:
        before, after = self.comparison_documents()
        runtime_reference = before["provenance"]["runtime_reference"]
        runtime_reference.update(
            {
                "commit": "unknown",
                "tree": "not-a-full-object-id",
                "exact_production_path_equality": False,
                "measured_production_paths_dirty": True,
            }
        )
        self.assert_comparison_provenance_rejected(
            before,
            after,
            [
                "before.runtime_reference.commit",
                "before.runtime_reference.tree",
                "before.runtime_reference.exact_production_path_equality",
                "before.runtime_reference.measured_production_paths_dirty",
            ],
        )

    def test_comparison_rejects_unknown_family_objects(self) -> None:
        before, after = self.comparison_documents()
        plugin_source = before["provenance"]["family_sources"]["plugin"]
        plugin_source["tree_object"] = "unknown"
        plugin_source["blobs"][1]["object"] = "1234"
        self.assert_comparison_provenance_rejected(
            before,
            after,
            [
                "before.family_sources.plugin.tree_object",
                "before.family_sources.plugin.blobs[1].object",
            ],
        )

    def test_wrong_but_valid_measured_root_is_structured_unsupported(self) -> None:
        before, after = self.comparison_documents()
        before["provenance"]["build"]["best_effort_provenance"][
            "cmake_source_root_matches_recorded_source"
        ] = False
        self.assert_comparison_provenance_rejected(
            before,
            after,
            ["before.build.best_effort_provenance.cmake_source_root_matches_recorded_source"],
        )

    def test_comparison_rejects_unresolved_cleanliness(self) -> None:
        before, after = self.comparison_documents()
        before["provenance"]["measured_checkout"]["dirty"] = None
        before["provenance"]["runtime_reference"]["measured_production_paths_dirty"] = None
        before["provenance"]["harness"]["dirty"] = None
        self.assert_comparison_provenance_rejected(
            before,
            after,
            [
                "before.measured_checkout.dirty",
                "before.runtime_reference.measured_production_paths_dirty",
                "before.harness.dirty",
            ],
        )

    def test_comparison_rejects_helper_from_wrong_valid_source_root(self) -> None:
        before, after = self.comparison_documents()
        provenance = before["provenance"]
        helper_build = provenance["benchmark_helper_build"]
        helper_build["cmake_source_root"] = "/test/other-valid-checkout"
        helper_build["cmake_cache"] = self.process_artifact("/test/other-valid-build/CMakeCache.txt")
        helper_build["best_effort_provenance"]["cmake_source_root_matches_recorded_source"] = False
        before["artifacts"]["benchmark_helper"] = self.process_artifact("/test/other-valid-build/tests/helper")
        provenance["binary_build_binding"] = self.module.process_binary_build_binding(
            provenance["build"], helper_build
        )
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "unsupported")
        self.assertEqual(comparison["reason_code"], "comparison_provenance_required")
        self.assertIn("before.benchmark_helper_build.cmake_source_root", comparison["mismatches"])
        self.assertIn(
            "before.benchmark_helper_build.best_effort_provenance.cmake_source_root_matches_recorded_source",
            comparison["mismatches"],
        )

    def test_comparison_rejects_helper_from_inconsistent_valid_cache(self) -> None:
        before, after = self.comparison_documents()
        provenance = before["provenance"]
        helper_build = provenance["benchmark_helper_build"]
        helper_build["cmake_cache"] = self.process_artifact("/test/other-valid-build/CMakeCache.txt")
        helper_build["generator"] = "Unix Makefiles"
        helper_build["recipe"]["generator"] = "Unix Makefiles"
        before["artifacts"]["benchmark_helper"] = self.process_artifact("/test/other-valid-build/tests/helper")
        provenance["binary_build_binding"] = self.module.process_binary_build_binding(
            provenance["build"], helper_build
        )
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "unsupported")
        self.assertEqual(comparison["reason_code"], "comparison_provenance_required")
        self.assertIn(
            "before.binary_build_binding.recorded_configuration_equivalent",
            comparison["mismatches"],
        )

    def test_comparison_rejects_dirty_harness(self) -> None:
        before, after = self.comparison_documents()
        before["provenance"]["harness"]["dirty"] = True
        self.assert_comparison_provenance_rejected(before, after, ["before.harness.dirty"])

    def test_comparison_rejects_unresolved_harness_identity(self) -> None:
        before, after = self.comparison_documents()
        harness = before["provenance"]["harness"]
        harness.update({"repository": "unknown", "commit": "unknown", "tree": ""})
        self.assert_comparison_provenance_rejected(
            before,
            after,
            ["before.harness.repository", "before.harness.commit", "before.harness.tree"],
        )

    def test_comparison_rejects_provenance_mismatch_and_same_authority(self) -> None:
        before, after = self.comparison_documents()
        after["provenance"]["host"]["boot_id_sha256"] = "b" * 64
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "unsupported")
        self.assertEqual(comparison["reason_code"], "incomparable_cohorts")

        before, after = self.comparison_documents()
        result_id = "family_plugin_lifecycle"
        index = self.module.PROCESS_EXPECTED_RESULT_IDS.index(result_id)
        provenance = after["provenance"]
        provenance["family_authorities"]["authorities"]["plugin"] = "legacy_local"
        provenance["build"]["features"]["family_authorities"]["plugin"] = "legacy_local"
        provenance["benchmark_helper_build"]["features"]["family_authorities"]["plugin"] = "legacy_local"
        provenance["binary_build_binding"] = self.module.process_binary_build_binding(
            provenance["build"], provenance["benchmark_helper_build"]
        )
        after["capabilities"]["plugin_authority"] = "legacy_local"
        after["results"][index] = self.measured_process_result(result_id, "legacy_local", 130.0)
        provenance["family_sources"]["plugin"] = self.process_family_sources()["plugin"]
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "unsupported")
        self.assertEqual(comparison["reason_code"], "authority_transition_required")
        self.assertFalse(comparison["repeatable_claim"])

    def test_comparison_binds_process_python_and_optional_provider_fixtures(self) -> None:
        for artifact_name in ("fake_process_child", "python"):
            with self.subTest(artifact=artifact_name):
                before, after = self.comparison_documents()
                after["artifacts"][artifact_name]["sha256"] = "f" * 64
                comparison = self.module.compare_process_documents(before, after)
                self.assertEqual(comparison["status"], "unsupported")
                self.assertEqual(comparison["reason_code"], "incomparable_cohorts")
                self.assertEqual(comparison["mismatches"], ["fixture_hashes"])

        before, after = self.comparison_documents()
        before["artifacts"]["fake_provider"] = self.process_artifact("/test/provider")
        after["artifacts"]["fake_provider"] = self.process_artifact("/test/provider")
        after["artifacts"]["fake_provider"]["sha256"] = "f" * 64
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "unsupported")
        self.assertEqual(comparison["reason_code"], "incomparable_cohorts")
        self.assertEqual(comparison["mismatches"], ["fixture_hashes"])

    def test_comparison_allows_expected_ava_and_helper_artifact_differences(self) -> None:
        before, after = self.comparison_documents()
        after["artifacts"]["ava"]["sha256"] = "a" * 64
        after["artifacts"]["benchmark_helper"]["sha256"] = "b" * 64
        before = self.module.json.loads(self.module.json.dumps(before, sort_keys=True))
        after = self.module.json.loads(self.module.json.dumps(after, sort_keys=True))
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "measured")

    def test_comparison_recomputes_stats_and_rejects_compatibility_change(self) -> None:
        before, after = self.comparison_documents()
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "measured")
        self.assertFalse(comparison["gating"])
        self.assertFalse(comparison["faster_required"])
        measured_ids = [item["id"] for item in comparison["comparisons"] if item["status"] == "measured"]
        self.assertEqual(measured_ids, ["family_plugin_lifecycle"])

        result_id = "family_plugin_lifecycle"
        index = self.module.PROCESS_EXPECTED_RESULT_IDS.index(result_id)
        after["results"][index]["samples"][0]["checks"]["expected_response"] = False
        after["results"][index]["compatibility_checks"]["expected_response"] = False
        comparison = self.module.compare_process_documents(before, after)
        item = next(item for item in comparison["comparisons"] if item["id"] == result_id)
        self.assertEqual(item["reason_code"], "compatibility_mismatch")

    def test_comparison_rejects_multiple_or_unexpected_authority_transitions(self) -> None:
        before, after = self.comparison_documents()
        provenance = after["provenance"]
        provenance["family_authorities"]["authorities"]["mcp"] = "supervised"
        provenance["build"]["features"]["family_authorities"]["mcp"] = "supervised"
        provenance["benchmark_helper_build"]["features"]["family_authorities"]["mcp"] = "supervised"
        provenance["binary_build_binding"] = self.module.process_binary_build_binding(
            provenance["build"], provenance["benchmark_helper_build"]
        )
        after["capabilities"]["mcp_authority"] = "supervised"
        result_id = "family_mcp_lifecycle"
        index = self.module.PROCESS_EXPECTED_RESULT_IDS.index(result_id)
        after["results"][index] = self.measured_process_result(result_id, "supervised", 130.0)
        after["provenance"]["family_sources"]["mcp"]["blobs"][0]["object"] = "a" * 40
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "unsupported")
        self.assertEqual(comparison["reason_code"], "single_authority_transition_required")
        self.assertEqual(
            comparison["mismatches"],
            ["family_authorities.plugin", "family_authorities.mcp"],
        )

        before, after = self.comparison_documents()
        for document, authority in ((before, "supervised"), (after, "legacy_local")):
            provenance = document["provenance"]
            provenance["family_authorities"]["authorities"]["plugin"] = authority
            provenance["build"]["features"]["family_authorities"]["plugin"] = authority
            provenance["benchmark_helper_build"]["features"]["family_authorities"]["plugin"] = authority
            provenance["binary_build_binding"] = self.module.process_binary_build_binding(
                provenance["build"], provenance["benchmark_helper_build"]
            )
            document["capabilities"]["plugin_authority"] = authority
        plugin_index = self.module.PROCESS_EXPECTED_RESULT_IDS.index("family_plugin_lifecycle")
        before["results"][plugin_index] = self.measured_process_result(
            "family_plugin_lifecycle", "supervised", 100.0
        )
        after["results"][plugin_index] = self.measured_process_result(
            "family_plugin_lifecycle", "legacy_local", 130.0
        )
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "unsupported")
        self.assertEqual(comparison["reason_code"], "unexpected_authority_transition")
        self.assertEqual(comparison["mismatches"], ["family_authorities.plugin"])

    def test_comparison_binds_source_authority_map_to_helper_capabilities(self) -> None:
        before, after = self.comparison_documents()
        before["capabilities"]["curl_authority"] = "legacy_local"
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "unsupported")
        self.assertEqual(comparison["reason_code"], "authority_attribution_mismatch")
        self.assertEqual(comparison["mismatches"], ["before.family_authorities.curl.capability"])

    def test_comparison_rejects_wrong_authority_and_source_attribution(self) -> None:
        before, after = self.comparison_documents()
        plugin_index = self.module.PROCESS_EXPECTED_RESULT_IDS.index("family_plugin_lifecycle")
        after["results"][plugin_index] = self.measured_process_result(
            "family_plugin_lifecycle", "legacy_local", 130.0
        )
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "unsupported")
        self.assertEqual(comparison["reason_code"], "authority_attribution_mismatch")
        self.assertEqual(comparison["mismatches"], ["after.family_authorities.plugin.result"])

        before, after = self.comparison_documents()
        after["provenance"]["family_sources"]["curl"]["tree_object"] = "b" * 40
        comparison = self.module.compare_process_documents(before, after)
        self.assertEqual(comparison["status"], "unsupported")
        self.assertEqual(comparison["reason_code"], "family_source_attribution_mismatch")
        self.assertEqual(comparison["mismatches"], ["family_sources.curl"])

    def test_comparison_rejects_matching_false_compatibility(self) -> None:
        before, after = self.comparison_documents()
        result_id = "family_plugin_lifecycle"
        index = self.module.PROCESS_EXPECTED_RESULT_IDS.index(result_id)
        for document in (before, after):
            checks = document["results"][index]["samples"][0]["checks"]
            checks["protocol_compatible"] = False
            checks["expected_response"] = False
            document["results"][index]["compatibility_checks"] = {
                "protocol_compatible": False,
                "expected_response": False,
            }
        comparison = self.module.compare_process_documents(before, after)
        item = next(item for item in comparison["comparisons"] if item["id"] == result_id)
        self.assertEqual(item["status"], "unsupported")
        self.assertEqual(item["reason_code"], "compatibility_mismatch")

    def test_historical_v2_artifact_still_validates(self) -> None:
        repository = pathlib.Path(self.script).resolve().parents[1]
        artifact = repository / "docs" / "engineering" / "backend-performance-baseline-2026-08-30.json"
        if not artifact.is_file():
            self.skipTest("historical v2 artifact is not present on the instrumentation carrier")
        document = self.module.json.loads(artifact.read_text(encoding="utf-8"))
        self.module.validate_document(document)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", required=True)
    parser.add_argument("--process-tests", action="store_true", help=argparse.SUPPRESS)
    arguments, remaining = parser.parse_known_args()
    BenchmarkHarnessTests.script = arguments.script
    program = unittest.main(argv=[sys.argv[0], *remaining], exit=False)
    return 0 if program.result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
