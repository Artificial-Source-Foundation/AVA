"""Regression coverage for the shared context-meter footer grammar."""

import re
import unittest

from tui_smoke_helpers import ACTIVE_CONTEXT_STATUS_PATTERN


class ContextStatusMatcherTest(unittest.TestCase):
    def test_independent_estimate_and_percentage(self):
        for amount in ("0", "42", "3.2k", "1m", "2.75m"):
            for estimate in ("", "~"):
                for percentage in ("", " (0%)", " (1.2%)", " (<0.1%)"):
                    value = estimate + amount + percentage
                    with self.subTest(value=value):
                        self.assertIsNotNone(re.fullmatch(ACTIVE_CONTEXT_STATUS_PATTERN, value))

    def test_malformed_context_rejected(self):
        for value in ("", "~", "~~3.2k", "-1k", ".2k", "3.k", "3..2k", "3.2kk", "3.2g",
                      "3.2k ()", "3.2k (1.2)", "3.2k (1..2%)", "3.2k (NaN%)", "3.2k (-1%)",
                      "3.2k (<1%)", "3.2k  (1%)", "3.2k (1%) extra", "unrelated status"):
            with self.subTest(value=value):
                self.assertIsNone(re.fullmatch(ACTIVE_CONTEXT_STATUS_PATTERN, value))

    def test_footer_boundaries_unchanged(self):
        for mode in ("Build", "Plan"):
            pattern = rf"{mode} · GPT-5\.5 · ctx {ACTIVE_CONTEXT_STATUS_PATTERN}"
            self.assertIsNotNone(re.fullmatch(pattern, f"{mode} · GPT-5.5 · ctx ~3.2k (1.2%)"))
            for footer in ("Other · GPT-5.5 · ctx ~3.2k (1.2%)", f"{mode} · Other · ctx ~3.2k (1.2%)",
                           f"{mode} GPT-5.5 ctx ~3.2k (1.2%)", f"{mode} · GPT-5.5 · ctx ~3.2k (1.2%) error"):
                with self.subTest(footer=footer):
                    self.assertIsNone(re.fullmatch(pattern, footer))


if __name__ == "__main__":
    unittest.main()
