#!/usr/bin/env python3
"""Focused tests for scripts/verify-markdown-links.py."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class MarkdownLinkVerifierTests(unittest.TestCase):
    script: Path

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name) / "repo"
        self.root.mkdir()

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write(self, relative: str, content: str = "") -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def run_verifier(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(self.script), str(self.root), *arguments],
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )

    def test_source_tree_accepts_links_from_docs_back_to_root(self) -> None:
        self.write("README.md", "root\n")
        self.write(
            "docs/guide.md",
            "[root](../README.md) [web](https://example.test/nope) "
            "[fragment](#section) ![image](missing.png)\n",
        )

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("source Markdown links: verified (2 files)", result.stdout)

    def test_source_tree_reports_missing_target(self) -> None:
        self.write("README.md", "[missing](docs/missing.md)\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 1)
        self.assertIn("README.md: missing local link target: docs/missing.md", result.stderr)

    def test_source_tree_rejects_decoded_escape(self) -> None:
        self.write("docs/guide.md", "[outside](%2e%2e/%2e%2e/outside.md)\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 1)
        self.assertIn("docs/guide.md: link escapes source tree", result.stderr)

    def test_source_tree_excludes_dependencies_builds_generated_and_caches(self) -> None:
        self.write("README.md", "[excluded reference](docs/reference-code/missing.md)\n")
        excluded_markdown = (
            ".git/README.md",
            "build/README.md",
            "build-debug/README.md",
            "cmake-build-release/README.md",
            "docs/reference-code/README.md",
            "docs/release-artifact-readme.md",
            "cmake/aicxx/README.md",
            "cwds/README.md",
            "enchantum/README.md",
            "src/json/README.md",
            "threadsafe/README.md",
            "utils/README.md",
            "generated/README.md",
            ".cache/README.md",
            "node_modules/package/README.md",
        )
        for relative in excluded_markdown:
            self.write(relative, "[broken](missing.md)\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("verified (1 files)", result.stdout)

    def test_source_tree_validates_percent_decoded_target(self) -> None:
        self.write("README.md", "[encoded](docs/space%20name.md)\n")
        self.write("docs/space name.md", "target\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_fenced_examples_are_ignored(self) -> None:
        self.write(
            "README.md",
            "````markdown\n[example](missing-one.md)\n```\n````\n"
            "~~~\n[other](missing-two.md)\n~~~\n",
        )

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_default_keeps_artifact_tree_discovery_and_messages(self) -> None:
        self.write("README.md", "[doc](docs/guide.md) ![optional image](missing.png)\n")
        self.write("docs/guide.md", "[root](../README.md)\n")
        # A source-tree exclusion must not alter the package check's traversal.
        self.write("node_modules/vendor/README.md", "[missing](absent.md)\n")

        failed = self.run_verifier()

        self.assertEqual(failed.returncode, 1)
        self.assertIn("staged Markdown link verification failed:", failed.stderr)
        self.assertIn("node_modules/vendor/README.md: missing local link target", failed.stderr)

        (self.root / "node_modules/vendor/absent.md").write_text("present\n", encoding="utf-8")
        passed = self.run_verifier()
        self.assertEqual(passed.returncode, 0, passed.stderr)
        self.assertIn("staged Markdown links: verified (4 files)", passed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", required=True, type=Path)
    arguments = parser.parse_args()
    MarkdownLinkVerifierTests.script = arguments.script.resolve(strict=True)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(MarkdownLinkVerifierTests)
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
