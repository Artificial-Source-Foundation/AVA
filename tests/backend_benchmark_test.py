#!/usr/bin/env python3
"""Focused self-tests for scripts/benchmark-backend.py."""

from __future__ import annotations

import argparse
import importlib.util
import os
import pathlib
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

    def test_environment_is_isolated_and_scrubbed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            home = pathlib.Path(temporary) / "home"
            with mock.patch.dict(
                os.environ,
                {
                    "AVA_SELF_TEST_KEEP": "yes",
                    "OPENAI_API_KEY": "must-not-leak",
                    "MY_PASSWORD": "must-not-leak",
                    "HTTPS_PROXY": "http://network.invalid",
                    "HOME": "/not/the/test/home",
                },
                clear=True,
            ):
                environment = self.module.isolated_environment(home)
        self.assertEqual(environment["AVA_SELF_TEST_KEEP"], "yes")
        self.assertEqual(environment["HOME"], str(home))
        self.assertEqual(environment["TMPDIR"], str(home / "tmp"))
        self.assertEqual(environment["AVA_BENCHMARK_OFFLINE"], "1")
        self.assertNotIn("OPENAI_API_KEY", environment)
        self.assertNotIn("MY_PASSWORD", environment)
        self.assertNotIn("HTTPS_PROXY", environment)

    def valid_document(self):
        return {
            "schema_version": self.module.SCHEMA_VERSION,
            "generated_at_utc": "2026-01-01T00:00:00+00:00",
            "git": {"commit": "abc", "dirty": False, "commit_with_state": "abc"},
            "host": {"os": "test", "kernel": "test", "cpu": "test", "ram_bytes": 1},
            "build": {"build_type": "test", "compiler": "test"},
            "binary": {"path": "/test/ava", "size_bytes": 1},
            "parameters": {
                "suite": "smoke",
                "runs": 1,
                "exact_command": ["benchmark"],
                "clock": "monotonic",
                "environment": "isolated",
            },
            "results": [
                self.module.unsupported(result_id, "self-test", "not measured", "self_test")
                for result_id in self.module.EXPECTED_RESULT_IDS
            ],
            "checks": [],
        }

    def test_schema_requires_every_family_and_machine_identity(self) -> None:
        document = self.valid_document()
        self.module.validate_document(document)
        document["results"] = document["results"][:-1]
        with self.assertRaisesRegex(ValueError, "missing"):
            self.module.validate_document(document)
        document = self.valid_document()
        del document["host"]
        with self.assertRaisesRegex(ValueError, "lacks host"):
            self.module.validate_document(document)

    def test_measured_schema_rejects_missing_statistics(self) -> None:
        document = self.valid_document()
        document["results"][0] = {
            "id": self.module.EXPECTED_RESULT_IDS[0],
            "family": "self-test",
            "status": "measured",
            "repetitions": 1,
            "samples": [{"value": 1.0}],
            "statistics": None,
        }
        with self.assertRaisesRegex(ValueError, "no samples"):
            self.module.validate_document(document)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", required=True)
    arguments, remaining = parser.parse_known_args()
    BenchmarkHarnessTests.script = arguments.script
    program = unittest.main(argv=[sys.argv[0], *remaining], exit=False)
    return 0 if program.result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
