#!/usr/bin/env python3
"""Focused tests for scripts/verify-assert-comments.py."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class AssertCommentTests(unittest.TestCase):
    script: Path

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name) / "repo"
        (self.root / "src/ava").mkdir(parents=True)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write_source(self, relative: str, content: str) -> Path:
        path = self.root / "src/ava" / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def run_checker(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(self.script), str(self.root)],
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )

    def test_compliant_assertion_passes(self) -> None:
        self.write_source(
            "ok.cpp",
            '#include "sys.h"\n\nvoid f()\n{\n  // The caller must pass a live session; fix the call site.\n  ASSERT(session);\n}\n',
        )
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("assert comments: verified (1 assertions in 1 files)", result.stdout)

    def test_missing_comment_fails(self) -> None:
        self.write_source("bad.cpp", "void f()\n{\n  ASSERT(session);\n}\n")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/ava/bad.cpp:3: ASSERT has no // or one-line /* ... */ comment", result.stderr)

    def test_blank_line_between_comment_and_assertion_fails(self) -> None:
        self.write_source("bad.cpp", "void f()\n{\n  // Explains the invariant.\n\n  ASSERT(session);\n}\n")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/ava/bad.cpp:5: ASSERT has no // or one-line /* ... */ comment", result.stderr)

    def test_blank_line_between_block_comment_and_assertion_fails(self) -> None:
        self.write_source("bad.cpp", "void f()\n{\n  /* Explains the invariant. */\n\n  ASSERT(session);\n}\n")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/ava/bad.cpp:5: ASSERT has no // or one-line /* ... */ comment", result.stderr)

    def test_one_line_block_comment_before_assertion_passes(self) -> None:
        self.write_source(
            "ok.cpp",
            "void f()\n{\n  /* The caller must pass a live session; fix the call site. */\n  ASSERT(session);\n}\n",
        )
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("assert comments: verified (1 assertions in 1 files)", result.stdout)

    def test_same_line_trailing_comment_fails(self) -> None:
        self.write_source("bad.cpp", "void f()\n{\n  ASSERT(session); // session must be live\n}\n")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/ava/bad.cpp:3: ASSERT has no // or one-line /* ... */ comment", result.stderr)

    def test_two_assertions_on_one_line_fail(self) -> None:
        self.write_source(
            "bad.cpp",
            "void f()\n{\n  // Both invariants explained.\n  ASSERT(first); ASSERT(second);\n}\n",
        )
        result = self.run_checker()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/ava/bad.cpp:4: 2 ASSERT uses on one line", result.stderr)
        self.assertIn("one ASSERT per line", result.stderr)

    def test_two_assertions_on_one_line_without_comment_fail(self) -> None:
        self.write_source("bad.cpp", "void f()\n{\n  ASSERT(first); ASSERT(second);\n}\n")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/ava/bad.cpp:3: 2 ASSERT uses on one line", result.stderr)

    def test_adjacent_assertions_each_need_their_own_comment(self) -> None:
        self.write_source(
            "bad.cpp",
            "void f()\n{\n  // First invariant explained.\n  ASSERT(first);\n  ASSERT(second);\n}\n",
        )
        result = self.run_checker()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/ava/bad.cpp:5: ASSERT has no // or one-line /* ... */ comment", result.stderr)

        self.write_source(
            "good.cpp",
            "void g()\n{\n  // First invariant explained.\n  ASSERT(first);\n  // Second invariant explained.\n  ASSERT(second);\n}\n",
        )
        (self.root / "src/ava/bad.cpp").unlink()
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2 assertions", result.stdout)

    def test_assert_in_line_comment_is_ignored(self) -> None:
        self.write_source("ok.cpp", "// An ASSERT(first) mention in prose is not a use.\nvoid f() {}\n")
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("0 assertions", result.stdout)

    def test_assert_in_block_comment_is_ignored(self) -> None:
        self.write_source("ok.cpp", "/* Mention ASSERT(first) here.\n   And ASSERT(second) there. */\nvoid f() {}\n")
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("0 assertions", result.stdout)

    def test_assert_in_string_literal_is_ignored(self) -> None:
        self.write_source(
            "ok.cpp",
            'char const* text = "ASSERT(first)";\nchar const* raw = R"(ASSERT(second))";\n',
        )
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("0 assertions", result.stdout)

    def test_digit_separator_does_not_hide_following_assertion(self) -> None:
        self.write_source("bad.cpp", "int timeout_ms = 60'000;\nASSERT(undocumented);\n")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/ava/bad.cpp:2: ASSERT has no", result.stderr)

    def test_digit_separator_does_not_hide_same_line_assertion(self) -> None:
        self.write_source("bad.cpp", "int timeout_ms = 60'000; ASSERT(undocumented);\n")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/ava/bad.cpp:1: ASSERT has no", result.stderr)

    def test_other_assert_macro_names_are_not_matched(self) -> None:
        self.write_source(
            "ok.cpp",
            "void f()\n{\n  AVA_ASSERT_TRUE(first);\n  MY_ASSERT(second);\n  ASSERTION(third);\n}\n",
        )
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("0 assertions", result.stdout)

    def test_assert_definition_is_not_a_use(self) -> None:
        self.write_source("debug.h", "#define ASSERT(condition) do {} while (0)\n")
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("0 assertions", result.stdout)

    def test_macro_body_use_with_block_comment_passes(self) -> None:
        self.write_source(
            "macro.cpp",
            "#define REQUIRE(condition)                  \\\n"
            "  {                                         \\\n"
            "    if (!(condition))                       \\\n"
            "    {                                       \\\n"
            "      /* The caller violated a precondition; fix the call site. */ \\\n"
            "      ASSERT(condition);                    \\\n"
            "    }                                       \\\n"
            "  }\n",
        )
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("1 assertions", result.stdout)

    def test_macro_body_use_with_line_comment_passes(self) -> None:
        self.write_source(
            "macro.cpp",
            "#define REQUIRE(condition)                  \\\n"
            "  {                                         \\\n"
            "    // The caller violated a precondition; fix the call site. \\\n"
            "    ASSERT(condition);                      \\\n"
            "  }\n",
        )
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("1 assertions", result.stdout)

    def test_non_source_extension_is_ignored(self) -> None:
        self.write_source("notes.txt", "ASSERT(undocumented);\n")
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("0 assertions in 0 files", result.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", required=True, type=Path)
    arguments = parser.parse_args()
    AssertCommentTests.script = arguments.script.resolve(strict=True)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(AssertCommentTests)
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
