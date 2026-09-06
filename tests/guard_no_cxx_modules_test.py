#!/usr/bin/env python3
"""Focused tests for scripts/guard-no-cxx-modules.sh.

Each fixture is a real temporary Ninja build directory so the guard's actual
`ninja -C <dir> -t commands` inspection path is exercised end to end.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

CLEAN_NINJA = """\
rule cxx
  command = g++ -O2 -c $in -o $out
build obj.o: cxx src.cc
"""

MODULES_TS_NINJA = """\
rule cxx
  command = g++ -fmodules-ts -fmodule-mapper= -fdeps-format=p1689r5 -c $in -o $out
build obj.o: cxx src.cc
"""

DEPS_ONLY_NINJA = """\
rule cxx
  command = clang++ -fdeps-format=p1689r5 -c $in -o $out
build obj.o: cxx src.cc
"""


class GuardNoCxxModulesTests(unittest.TestCase):
    script: Path
    ninja: Path

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def make_build_dir(self, name: str, build_ninja: str | None) -> Path:
        build_dir = self.root / name
        build_dir.mkdir()
        if build_ninja is not None:
            (build_dir / "build.ninja").write_text(build_ninja, encoding="utf-8")
        return build_dir

    def run_guard(self, build_dir: Path) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env["NINJA"] = str(self.ninja)
        return subprocess.run(
            ["bash", str(self.script), str(build_dir)],
            check=False,
            capture_output=True,
            text=True,
            timeout=15,
            env=env,
        )

    def test_clean_commands_pass(self) -> None:
        result = self.run_guard(self.make_build_dir("clean", CLEAN_NINJA))

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("no -fmodules*/-fmodule-*/-fdeps-* flags", result.stdout)

    def test_modules_ts_and_mapper_flags_are_rejected(self) -> None:
        result = self.run_guard(self.make_build_dir("modules", MODULES_TS_NINJA))

        self.assertEqual(result.returncode, 1)
        self.assertIn("module scanner flags unsupported", result.stderr)
        self.assertIn("-fmodules-ts", result.stderr)
        self.assertIn("-fmodule-mapper=", result.stderr)
        self.assertIn("-fdeps-format=p1689r5", result.stderr)

    def test_deps_flag_alone_is_rejected(self) -> None:
        result = self.run_guard(self.make_build_dir("deps", DEPS_ONLY_NINJA))

        self.assertEqual(result.returncode, 1)
        self.assertIn("-fdeps-format=p1689r5", result.stderr)

    def test_ninja_failure_is_not_hidden(self) -> None:
        result = self.run_guard(self.make_build_dir("unconfigured", None))

        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("no -fmodules*/-fmodule-*/-fdeps-* flags", result.stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--script", required=True, type=Path)
    parser.add_argument("--ninja", required=True, type=Path)
    args = parser.parse_args()
    GuardNoCxxModulesTests.script = args.script.resolve()
    GuardNoCxxModulesTests.ninja = args.ninja.resolve()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(GuardNoCxxModulesTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
