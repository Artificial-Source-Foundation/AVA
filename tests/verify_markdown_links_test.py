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
            "# Section\n\n[root](../README.md) [web](https://example.test/nope) "
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
            "docs/operations/release-artifact-readme.md",
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

    def test_source_tree_excludes_gitignored_local_plans(self) -> None:
        self.write("README.md", "root\n")
        self.write(".plans/local-plan.md", "[broken](missing.md)\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("source Markdown links: verified (1 files)", result.stdout)

    def test_source_tree_validates_percent_decoded_target(self) -> None:
        self.write("README.md", "[encoded](docs/space%20name.md)\n")
        self.write("docs/space name.md", "target\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_source_tree_accepts_existing_self_github_link(self) -> None:
        self.write(
            "README.md",
            "[guide](https://github.com/Artificial-Source/AVA/blob/develop/docs/guide.md#part)\n",
        )
        self.write("docs/guide.md", "# Part\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_source_tree_reports_missing_self_github_anchor(self) -> None:
        self.write(
            "README.md",
            "[missing](https://github.com/Artificial-Source/AVA/blob/develop/docs/guide.md#missing)\n",
        )
        self.write("docs/guide.md", "# Present\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "README.md: missing local Markdown anchor: docs/guide.md#missing",
            result.stderr,
        )

    def test_source_tree_reports_missing_self_github_target(self) -> None:
        self.write(
            "README.md",
            "[missing](https://github.com/Artificial-Source/AVA/blob/develop/docs/reference-code/missing.md)\n",
        )

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 1)
        self.assertIn("missing local link target", result.stderr)

    def test_source_tree_decodes_self_github_target(self) -> None:
        self.write(
            "README.md",
            "[guide](https://github.com/Artificial-Source/AVA/blob/develop/docs/space%20name.md)\n",
        )
        self.write("docs/space name.md", "guide\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_source_tree_rejects_escaping_self_github_target(self) -> None:
        self.write(
            "README.md",
            "[outside](https://github.com/Artificial-Source/AVA/blob/develop/%2e%2e/outside.md)\n",
        )

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 1)
        self.assertIn("README.md: link escapes source tree", result.stderr)

    def test_artifact_mode_treats_self_github_link_as_external(self) -> None:
        self.write(
            "README.md",
            "[external](https://github.com/Artificial-Source/AVA/blob/develop/docs/missing.md)\n",
        )

        result = self.run_verifier()

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_reference_definition_reports_missing_local_target(self) -> None:
        self.write("README.md", "Read the [guide][guide].\n\n[guide]: docs/missing.md\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 1)
        self.assertIn("README.md: missing local link target: docs/missing.md", result.stderr)

    def test_local_reference_resolves_definition_after_usage(self) -> None:
        self.write("README.md", "Read the [guide][].\n\n[guide]: docs/guide.md\n")
        self.write("docs/guide.md", "guide\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_reference_links_validate_local_markdown_anchors(self) -> None:
        self.write(
            "README.md",
            "Read [full][guide] and [collapsed][].\n\n"
            "[guide]: docs/guide.md#target\n"
            "[collapsed]: docs/guide.md#target\n",
        )
        self.write("docs/guide.md", "# Target\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_reference_usage_reports_missing_definition(self) -> None:
        self.write("README.md", "Read the [missing guide][not defined].\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "README.md: undefined reference link label: not defined",
            result.stderr,
        )

    def test_reference_labels_ignore_case_and_collapse_whitespace(self) -> None:
        self.write(
            "README.md",
            "Read the [guide][  MIXED   label ].\n\n[mixed label]: docs/guide.md\n",
        )
        self.write("docs/guide.md", "guide\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_external_reference_target_is_ignored(self) -> None:
        self.write(
            "README.md",
            "Read the [website][site].\n\n[site]: https://example.test/missing\n",
        )

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_reference_style_image_is_ignored(self) -> None:
        self.write("README.md", "![optional image][logo]\n\n[logo]: missing.png\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_same_page_anchor_is_validated(self) -> None:
        self.write("README.md", "# Present section\n\n[valid](#present-section)\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_same_page_anchor_is_reported(self) -> None:
        self.write("README.md", "# Present section\n\n[missing](#absent-section)\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "README.md: missing local Markdown anchor: README.md#absent-section",
            result.stderr,
        )

    def test_cross_page_anchor_is_validated(self) -> None:
        self.write("README.md", "[valid](docs/guide.md#present-section)\n")
        self.write("docs/guide.md", "# Present section\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_cross_page_anchor_is_reported(self) -> None:
        self.write("README.md", "[missing](docs/guide.md#absent-section)\n")
        self.write("docs/guide.md", "# Present section\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "README.md: missing local Markdown anchor: docs/guide.md#absent-section",
            result.stderr,
        )

    def test_duplicate_headings_receive_numeric_suffixes(self) -> None:
        self.write(
            "README.md",
            "[first](docs/guide.md#repeat) [second](docs/guide.md#repeat-1) "
            "[third](docs/guide.md#repeat-2)\n",
        )
        self.write("docs/guide.md", "# Repeat\n\n## Repeat\n\n### Repeat\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_duplicate_heading_suffixes_avoid_existing_slug_collisions(self) -> None:
        self.write(
            "README.md",
            "[first](docs/guide.md#repeat) [second](docs/guide.md#repeat-1) "
            "[collision](docs/guide.md#repeat-1-1) "
            "[third](docs/guide.md#repeat-2)\n",
        )
        self.write(
            "docs/guide.md",
            "# Repeat\n\n## Repeat\n\n## Repeat-1\n\n### Repeat\n",
        )

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_heading_anchors_decode_entities_and_render_inline_formatting(self) -> None:
        self.write(
            "README.md",
            "[formatted](docs/guide.md#use-bold-display-code--emphasis)\n",
        )
        self.write(
            "docs/guide.md",
            "# Use **Bold**, [display](https://example.test), `Code` &amp; _Emphasis_!\n",
        )

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_percent_decoded_fragment_is_validated(self) -> None:
        self.write("README.md", "[encoded](docs/guide.md#caf%C3%A9)\n")
        self.write("docs/guide.md", "# Café\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_fenced_heading_does_not_create_an_anchor(self) -> None:
        self.write(
            "README.md",
            "# Visible\n\n```markdown\n# Hidden\n```\n\n[hidden](#hidden)\n",
        )

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "README.md: missing local Markdown anchor: README.md#hidden",
            result.stderr,
        )

    def test_explicit_html_id_and_name_anchors_are_validated(self) -> None:
        self.write(
            "README.md",
            '<a id="stable-id"></a>\n<span name=legacy-name></span>\n\n'
            "[id](#stable-id) [name](#legacy-name)\n",
        )

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_non_markdown_fragment_is_not_validated(self) -> None:
        self.write("README.md", "[data](data.json#missing)\n")
        self.write("data.json", "{}\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_external_fragment_is_not_validated(self) -> None:
        self.write("README.md", "[external](https://example.test/missing#fragment)\n")

        result = self.run_verifier("--source-tree")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_artifact_mode_validates_local_markdown_anchors(self) -> None:
        self.write("README.md", "[section](docs/guide.md#missing)\n")
        guide = self.write("docs/guide.md", "# Present\n")

        failed = self.run_verifier()

        self.assertEqual(failed.returncode, 1)
        self.assertIn(
            "README.md: missing local Markdown anchor: docs/guide.md#missing",
            failed.stderr,
        )

        guide.write_text("# Missing\n", encoding="utf-8")
        passed = self.run_verifier()
        self.assertEqual(passed.returncode, 0, passed.stderr)

    def test_fenced_examples_are_ignored(self) -> None:
        self.write(
            "README.md",
            "````markdown\n[example](missing-one.md)\n```\n````\n"
            "~~~\n[other](missing-two.md)\n[guide][missing]\n"
            "[missing]: missing-three.md\n~~~\n",
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
