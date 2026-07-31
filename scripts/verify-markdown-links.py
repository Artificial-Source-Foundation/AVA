#!/usr/bin/env python3
"""Verify local links in staged or first-party source Markdown."""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import sys
import urllib.parse

LINK_RE = re.compile(r"(?<!!)\[[^\]]*\]\(\s*(<[^>]+>|[^\s)]+)(?:\s+[^)]*)?\)")
FENCE_RE = re.compile(r"^\s*(`{3,}|~{3,})")

# These trees are dependencies, generated output, or caches rather than AVA's
# first-party source documentation. Exact repository-relative exclusions avoid
# accidentally hiding similarly named first-party directories elsewhere.
SOURCE_EXCLUDED_PREFIXES = (
    (".git",),
    ("cmake", "aicxx"),
    ("cwds",),
    ("docs", "reference-code"),
    ("enchantum",),
    ("src", "json"),
    ("threadsafe",),
    ("utils",),
)
SOURCE_EXCLUDED_NAMES = frozenset(
    {
        ".cache",
        ".mypy_cache",
        ".pytest_cache",
        "__pycache__",
        "_deps",
        "CMakeFiles",
        "generated",
        "node_modules",
    }
)
# This file is relocated to the artifact root and is checked there by the
# package verifier; its links intentionally do not resolve from its source path.
SOURCE_EXCLUDED_FILES = frozenset({("docs", "release-artifact-readme.md")})


def markdown_without_fenced_code(text: str) -> str:
    lines: list[str] = []
    fence_character: str | None = None
    fence_length = 0
    for line in text.splitlines():
        match = FENCE_RE.match(line)
        if match:
            marker = match.group(1)
            if fence_character is None:
                fence_character = marker[0]
                fence_length = len(marker)
                lines.append("")
                continue
            if (
                marker[0] == fence_character
                and len(marker) >= fence_length
                and not line[match.end() :].strip()
            ):
                fence_character = None
                fence_length = 0
                lines.append("")
                continue
        lines.append("" if fence_character else line)
    return "\n".join(lines)


def has_prefix(parts: tuple[str, ...], prefix: tuple[str, ...]) -> bool:
    return parts[: len(prefix)] == prefix


def is_build_tree_name(name: str) -> bool:
    return name == "build" or name.startswith("build-") or name.startswith("cmake-build-")


def source_path_is_excluded(relative: pathlib.PurePath, *, directory: bool = False) -> bool:
    parts = relative.parts
    if any(has_prefix(parts, prefix) for prefix in SOURCE_EXCLUDED_PREFIXES):
        return True
    if parts in SOURCE_EXCLUDED_FILES:
        return True
    if any(part in SOURCE_EXCLUDED_NAMES or is_build_tree_name(part) for part in parts):
        return True
    return directory and relative.name == ".git"


def source_markdown_files(root: pathlib.Path) -> list[pathlib.Path]:
    markdown_files: list[pathlib.Path] = []
    for directory, directory_names, file_names in os.walk(root, followlinks=False):
        current = pathlib.Path(directory)
        kept_directories = []
        for name in directory_names:
            relative = (current / name).relative_to(root)
            if not source_path_is_excluded(relative, directory=True):
                kept_directories.append(name)
        directory_names[:] = kept_directories
        for name in file_names:
            if not name.endswith(".md"):
                continue
            path = current / name
            if not source_path_is_excluded(path.relative_to(root)):
                markdown_files.append(path)
    return sorted(markdown_files)


def verify(root: pathlib.Path, *, source_tree: bool) -> tuple[list[str], int]:
    failures: list[str] = []
    markdown_files = source_markdown_files(root) if source_tree else sorted(root.rglob("*.md"))
    if not markdown_files:
        tree_kind = "source" if source_tree else "artifact documentation"
        failures.append(f"{tree_kind} tree contains no Markdown files")

    for markdown in markdown_files:
        text = markdown_without_fenced_code(markdown.read_text(encoding="utf-8"))
        for match in LINK_RE.finditer(text):
            raw_target = match.group(1).strip("<>")
            parsed = urllib.parse.urlsplit(raw_target)
            if parsed.scheme or parsed.netloc or raw_target.startswith("//") or not parsed.path:
                continue
            decoded_path = urllib.parse.unquote(parsed.path)
            candidate = (markdown.parent / decoded_path).resolve(strict=False)
            try:
                relative_candidate = candidate.relative_to(root)
            except ValueError:
                tree_kind = "source tree" if source_tree else "artifact docs"
                failures.append(f"{markdown.relative_to(root)}: link escapes {tree_kind}: {raw_target}")
                continue
            if source_tree and source_path_is_excluded(relative_candidate):
                continue
            if not candidate.exists():
                failures.append(f"{markdown.relative_to(root)}: missing local link target: {raw_target}")

    return failures, len(markdown_files)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=pathlib.Path)
    parser.add_argument(
        "--source-tree",
        action="store_true",
        help="validate first-party Markdown in a repository source tree",
    )
    args = parser.parse_args()

    root = args.root.resolve(strict=True)
    if not root.is_dir():
        parser.error(f"root is not a directory: {root}")

    failures, markdown_count = verify(root, source_tree=args.source_tree)
    if failures:
        label = "source" if args.source_tree else "staged"
        print(f"{label} Markdown link verification failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    label = "source" if args.source_tree else "staged"
    print(f"{label} Markdown links: verified ({markdown_count} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
