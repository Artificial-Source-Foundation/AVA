#!/usr/bin/env python3
"""Static regression checks for the pin-only bootstrap contract.

The checks deliberately read text only: autogen.sh and dependency code are never
executed by this test.
"""
from __future__ import annotations

import configparser
import pathlib
import re
import unittest

SOURCE = pathlib.Path(__file__).resolve().parents[1]
AUTOGEN = SOURCE / "autogen.sh"
GITMODULES = SOURCE / ".gitmodules"


class BootstrapSubmoduleContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.script = AUTOGEN.read_text(encoding="utf-8")

    def test_only_pinned_checkout_initialization_is_allowed(self) -> None:
        command_lines = [
            line.strip()
            for line in self.script.splitlines()
            if re.match(r"^(?:if ! )?git submodule update\b", line.strip())
        ]
        self.assertEqual(
            command_lines,
            ["if ! git submodule update --init --checkout --recursive; then"],
        )

        forbidden_mutations = (
            r"\bgit\s+pull\b",
            r"\bgit\s+fetch\b",
            r"\bgit\s+merge\b",
            r"\bgit\s+commit\b",
            r"\bgit\s+add\b",
            r"\bgit\s+update-index\b",
            r"git\s+submodule\s+update[^\n]*--remote",
            r"git\s+submodule\s+set-branch\b",
            r"(^|[;&|]\s*)exec\b",
            r"\b(?:cp|mv|install)\b[^\n]*(?:autogen\.sh|\$0)",
            r"\bsed\b[^\n]*-i[^\n]*(?:autogen\.sh|\$0)",
            r">+\s*(?:autogen\.sh|\$0)\b",
        )
        for pattern in forbidden_mutations:
            with self.subTest(pattern=pattern):
                self.assertIsNone(re.search(pattern, self.script, re.MULTILINE))

    def test_maintainer_helper_is_checked_but_never_invoked(self) -> None:
        references = [
            line.strip()
            for line in self.script.splitlines()
            if "real_maintainer.sh" in line
        ]
        self.assertEqual(
            references,
            ["if test ! -e cmake/aicxx/scripts/real_maintainer.sh; then"],
        )

    def test_status_failures_and_pin_mismatches_fail_closed(self) -> None:
        status_probe = 'if ! SUBMODULE_STATUS="$(git submodule status --recursive)"; then'
        self.assertEqual(self.script.count(status_probe), 2)
        self.assertEqual(self.script.count("grep -E '^[+U]'"), 2)
        self.assertIn("grep '^-'", self.script)

        for match in re.finditer(r"grep -E '\^\[\+U\]'", self.script):
            enforcement_block = self.script[match.end():match.end() + 260]
            self.assertIn("exit 1", enforcement_block)
        update = self.script.index("if ! git submodule update --init --checkout --recursive; then")
        self.assertLess(self.script.index("grep -E '^[+U]'"), update)
        self.assertGreater(self.script.rindex(status_probe), update)

    def test_all_submodules_disable_mutable_branch_tracking(self) -> None:
        config = configparser.ConfigParser()
        config.read(GITMODULES, encoding="utf-8")
        self.assertTrue(config.sections())
        for section in config.sections():
            with self.subTest(section=section):
                self.assertNotIn("branch", config[section])
                self.assertEqual(config[section].get("update"), "checkout")
        memory = config['submodule "memory"']
        self.assertEqual(memory["path"], "memory")
        self.assertEqual(memory["url"], "https://github.com/CarloWood/memory.git")


if __name__ == "__main__":
    unittest.main()
