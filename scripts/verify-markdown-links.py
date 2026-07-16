#!/usr/bin/env python3
"""Verify that staged Markdown relative links resolve within one artifact tree."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
import urllib.parse

LINK_RE = re.compile(r"!?\[[^\]]*\]\(\s*(<[^>]+>|[^\s)]+)(?:\s+[^)]*)?\)")
FENCE_RE = re.compile(r"^\s*(```|~~~)")


def markdown_without_fenced_code(text: str) -> str:
    lines: list[str] = []
    fence: str | None = None
    for line in text.splitlines():
        match = FENCE_RE.match(line)
        if match:
            marker = match.group(1)
            if fence is None:
                fence = marker
            elif marker == fence:
                fence = None
            lines.append("")
            continue
        lines.append("" if fence else line)
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=pathlib.Path)
    args = parser.parse_args()

    root = args.root.resolve(strict=True)
    if not root.is_dir():
        parser.error(f"root is not a directory: {root}")

    failures: list[str] = []
    markdown_files = sorted(root.rglob("*.md"))
    if not markdown_files:
        failures.append("artifact documentation tree contains no Markdown files")

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
                candidate.relative_to(root)
            except ValueError:
                failures.append(f"{markdown.relative_to(root)}: link escapes artifact docs: {raw_target}")
                continue
            if not candidate.exists():
                failures.append(f"{markdown.relative_to(root)}: missing local link target: {raw_target}")

    if failures:
        print("staged Markdown link verification failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(f"staged Markdown links: verified ({len(markdown_files)} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
