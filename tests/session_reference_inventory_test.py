#!/usr/bin/env python3
"""Reject unapproved ``Session&`` spellings in AVA C++ sources.

The use of ``Session&`` or ``Session const&`` anywhere in the code is prohibited:
always pass a ``session_ts&`` or ``session_ts const&`` instead.

This scan only covers .cpp, .cxx, and .h files below src/ava and tests.
A matching line is accepted only when a trailing C++ comment on that same line
contains the words "is allowed", keeping the small set of intentional exceptions
visible beside the code. ``Session&&`` also contains ``Session&`` and is
therefore subject to the same rule.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


SESSION_REFERENCE = re.compile(r"\bSession(?: const)?&")
ALLOWED_COMMENT = re.compile(r"//.*\bis allowed\b")
SOURCE_SUFFIXES = {".cpp", ".cxx", ".h"}


def violations_below(root: pathlib.Path, relative_directory: str) -> list[str]:
    """Return formatted violations found below ``relative_directory`` in ``root``.

    Files are inspected line by line so an exception comment authorizes only
    matches on its own line. Results use repository-relative paths and are
    sorted by the directory traversal for deterministic CTest diagnostics.
    """
    directory = root / relative_directory
    violations: list[str] = []
    for path in sorted(directory.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            if SESSION_REFERENCE.search(line) and not ALLOWED_COMMENT.search(line):
                violations.append(f"{path.relative_to(root)}:{line_number}:{line}")
    return violations


def main() -> int:
    """Parse the repository root, scan both source areas, and report offenders."""
    parser = argparse.ArgumentParser(description="Reject unapproved Session reference spellings.")
    parser.add_argument("--source", required=True, type=pathlib.Path, help="AVA repository root")
    args = parser.parse_args()
    source = args.source.absolute()

    violations = violations_below(source, "src/ava")
    violations.extend(violations_below(source, "tests"))
    if violations:
        print(
            "Found `Session&` or `Session const&` without a same-line '// ... is allowed ...' comment:\n  "
            + "\n  ".join(violations),
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
