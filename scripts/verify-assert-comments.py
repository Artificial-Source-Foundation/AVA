#!/usr/bin/env python3
"""Verify that every literal ASSERT(...) under src/ava has an actionable comment.

AVA's assertion policy requires every individual ASSERT to be preceded, on the
immediately preceding physical line, by its own actionable comment that
explains what the developer did wrong and how to correct it. The comment is
normally a `//` line; a one-line `/* ... */` comment is also accepted so that
an ASSERT inside a continued macro definition can carry the required comment.
Each ASSERT needs its own comment, so a physical line may contain at most one
ASSERT use. This checker enforces the placement rule mechanically; reviewers
still judge whether the comment is actionable.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import sys

SOURCE_EXTENSIONS = frozenset({".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"})
ASSERT_USE_RE = re.compile(r"(?<![A-Za-z0-9_])ASSERT\s*\(")
ASSERT_DEFINITION_RE = re.compile(r"^\s*#\s*define\s+ASSERT\b")
COMMENT_LINE_RE = re.compile(r"^\s*//")
# A one-line block comment, optionally followed by a macro-continuation backslash.
BLOCK_COMMENT_LINE_RE = re.compile(r"^\s*/\*.*\*/\s*\\?\s*$")


def is_digit_separator(line: str, index: int) -> bool:
    """Return whether line[index] is an apostrophe inside a numeric literal."""
    if index == 0 or index + 1 >= len(line) or not line[index + 1].isalnum():
        return False
    start = index
    while start > 0 and (line[start - 1].isalnum() or line[start - 1] in "._"):
        start -= 1
    prefix = line[start:index]
    return bool(prefix) and (prefix[0].isdigit() or (prefix[0] == "." and len(prefix) > 1 and prefix[1].isdigit()))


def code_without_comments_and_literals(lines: list[str]) -> list[str]:
    """Blank out comments and string/character literal contents per line.

    The result has one entry per physical line; an entry contains only the
    characters that are real code on that line. This is a lightweight lexer
    that handles `//` and `/* ... */` comments, regular and raw string
    literals, and character literals; it is deliberately not a full C++ lexer.
    """
    result: list[str] = []
    in_block_comment = False
    in_string = False
    in_char = False
    raw_string_delimiter: str | None = None

    for line in lines:
        code: list[str] = []
        index = 0
        length = len(line)
        while index < length:
            if in_block_comment:
                end = line.find("*/", index)
                if end == -1:
                    index = length
                else:
                    in_block_comment = False
                    index = end + 2
            elif raw_string_delimiter is not None:
                terminator = ")" + raw_string_delimiter + '"'
                end = line.find(terminator, index)
                if end == -1:
                    index = length
                else:
                    raw_string_delimiter = None
                    index = end + len(terminator)
            elif in_string:
                if line[index] == "\\":
                    index += 2
                    continue
                if line[index] == '"':
                    in_string = False
                index += 1
            elif in_char:
                if line[index] == "\\":
                    index += 2
                    continue
                if line[index] == "'":
                    in_char = False
                index += 1
            elif line.startswith("//", index):
                index = length
            elif line.startswith("/*", index):
                in_block_comment = True
                index += 2
            elif line.startswith('R"', index):
                open_paren = line.find("(", index + 2)
                if open_paren == -1:
                    # Malformed or split raw-string opener; drop the rest of the line.
                    index = length
                else:
                    raw_string_delimiter = line[index + 2 : open_paren]
                    index = open_paren + 1
            elif line[index] == '"':
                in_string = True
                index += 1
            elif line[index] == "'" and is_digit_separator(line, index):
                code.append(line[index])
                index += 1
            elif line[index] == "'":
                in_char = True
                index += 1
            else:
                code.append(line[index])
                index += 1
        result.append("".join(code))
    return result


def source_files(root: pathlib.Path) -> list[pathlib.Path]:
    source_root = root / "src" / "ava"
    files: list[pathlib.Path] = []
    for directory, directory_names, file_names in os.walk(source_root, followlinks=False):
        directory_names.sort()
        for name in sorted(file_names):
            if pathlib.Path(name).suffix in SOURCE_EXTENSIONS:
                files.append(pathlib.Path(directory) / name)
    return files


def verify(root: pathlib.Path) -> tuple[list[str], int, int]:
    failures: list[str] = []
    assertion_count = 0
    files = source_files(root)
    for path in files:
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        code_lines = code_without_comments_and_literals(lines)
        relative = path.relative_to(root).as_posix()
        for index, code in enumerate(code_lines):
            if ASSERT_DEFINITION_RE.match(lines[index]):
                continue
            uses = ASSERT_USE_RE.findall(code)
            if not uses:
                continue
            assertion_count += len(uses)
            if len(uses) > 1:
                failures.append(
                    f"{relative}:{index + 1}: {len(uses)} ASSERT uses on one line; write one ASSERT per line so each "
                    "assertion has its own immediately preceding actionable comment"
                )
                continue
            previous = lines[index - 1] if index > 0 else ""
            if not (COMMENT_LINE_RE.match(previous) or BLOCK_COMMENT_LINE_RE.match(previous)):
                failures.append(
                    f"{relative}:{index + 1}: ASSERT has no // or one-line /* ... */ comment on the immediately preceding line"
                )
    return failures, assertion_count, len(files)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=pathlib.Path, help="repository root containing src/ava")
    args = parser.parse_args()

    root = args.root.resolve(strict=True)
    if not (root / "src" / "ava").is_dir():
        parser.error(f"repository root has no src/ava directory: {root}")

    failures, assertion_count, file_count = verify(root)
    if failures:
        print("assert comment verification failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(f"assert comments: verified ({assertion_count} assertions in {file_count} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
