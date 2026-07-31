#!/usr/bin/env python3
"""Verify local links in staged or first-party source Markdown."""

from __future__ import annotations

import argparse
import html
import os
import pathlib
import re
import sys
import unicodedata
import urllib.parse

LINK_RE = re.compile(r"(?<!!)\[[^\]]*\]\(\s*(<[^>]+>|[^\s)]+)(?:\s+[^)]*)?\)")
REFERENCE_DEFINITION_RE = re.compile(
    r"^[ ]{0,3}\[([^\]\n]{1,999})\]:[ \t]*(?:<([^>\n]*)>|([^ \t\n]+))",
    re.MULTILINE,
)
REFERENCE_USAGE_RE = re.compile(
    r"(?<!!)\[([^\]\n]{1,999})\]\[([^\]\n]{0,999})\]"
)
FENCE_RE = re.compile(r"^\s*(`{3,}|~{3,})")
ATX_HEADING_RE = re.compile(r"^[ \t]{0,3}#{1,6}(?:[ \t]+|$)(.*)$")
HEADING_INLINE_LINK_RE = re.compile(
    r"!?\[([^\]\n]*)\]\(\s*(?:<[^>\n]+>|[^)\n]*)\)"
)
HEADING_REFERENCE_LINK_RE = re.compile(r"!?\[([^\]\n]*)\]\[[^\]\n]*\]")
UNDERSCORE_EMPHASIS_RE = re.compile(
    r"(?<![\w\\])(_{1,3})(?=\S)(.+?\S)\1(?!\w)"
)
HTML_TAG_RE = re.compile(r"<[A-Za-z][^>\n]*>")
HTML_ANCHOR_ATTRIBUTE_RE = re.compile(
    r'''(?:^|\s)(?:id|name)\s*=\s*(?:"([^"]+)"|'([^']+)'|([^\s"'=<>`]+))''',
    re.IGNORECASE,
)
SLUG_REMOVED_ASCII = frozenset("!\"#$%&'()*+,./:;<=>?@[\\]^`{|}~")

# These trees are dependencies, generated output, or caches rather than AVA's
# first-party source documentation. Exact repository-relative exclusions avoid
# accidentally hiding similarly named first-party directories elsewhere.
SOURCE_EXCLUDED_PREFIXES = (
    (".git",),
    (".plans",),
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
SOURCE_EXCLUDED_FILES = frozenset(
    {("docs", "operations", "release-artifact-readme.md")}
)

SELF_GITHUB_SCHEME = "https"
SELF_GITHUB_HOST = "github.com"
SELF_GITHUB_PATH_PREFIX = "/Artificial-Source/AVA/blob/develop/"


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


def normalize_reference_label(label: str) -> str:
    return " ".join(label.split()).casefold()


def github_heading_slug(raw_heading: str) -> str:
    heading = HEADING_INLINE_LINK_RE.sub(r"\1", raw_heading)
    heading = HEADING_REFERENCE_LINK_RE.sub(r"\1", heading)
    heading = HTML_TAG_RE.sub("", heading)
    heading = html.unescape(heading)

    previous = None
    while heading != previous:
        previous = heading
        heading = UNDERSCORE_EMPHASIS_RE.sub(r"\2", heading)

    heading = heading.lower().strip()
    heading = "".join(
        character
        for character in heading
        if character not in SLUG_REMOVED_ASCII
        and not (
            unicodedata.category(character).startswith("P")
            and character not in "-_"
        )
    )
    return re.sub(r"\s", "-", heading)


def markdown_anchors(text: str) -> frozenset[str]:
    text = markdown_without_fenced_code(text)
    anchors: set[str] = set()

    for tag_match in HTML_TAG_RE.finditer(text):
        tag = tag_match.group(0)
        for attribute_match in HTML_ANCHOR_ATTRIBUTE_RE.finditer(tag):
            anchor = next(
                value for value in attribute_match.groups() if value is not None
            )
            anchors.add(html.unescape(anchor))

    slug_counts: dict[str, int] = {}
    generated_slugs: set[str] = set()
    for line in text.splitlines():
        heading_match = ATX_HEADING_RE.match(line)
        if heading_match is None:
            continue
        heading = re.sub(r"[ \t]+#+[ \t]*$", "", heading_match.group(1))
        slug = github_heading_slug(heading)
        if not slug:
            continue
        duplicate_count = slug_counts.get(slug, 0)
        candidate = slug if duplicate_count == 0 else f"{slug}-{duplicate_count}"
        while candidate in generated_slugs:
            duplicate_count += 1
            candidate = f"{slug}-{duplicate_count}"
        slug_counts[slug] = duplicate_count + 1
        generated_slugs.add(candidate)
        anchors.add(candidate)

    return frozenset(anchors)


def validate_link(
    raw_target: str,
    markdown: pathlib.Path,
    root: pathlib.Path,
    *,
    source_tree: bool,
    anchor_cache: dict[pathlib.Path, frozenset[str]],
    failures: list[str],
) -> None:
    parsed = urllib.parse.urlsplit(raw_target)
    self_github_link = (
        source_tree
        and parsed.scheme == SELF_GITHUB_SCHEME
        and parsed.netloc == SELF_GITHUB_HOST
        and parsed.path.startswith(SELF_GITHUB_PATH_PREFIX)
    )
    if self_github_link:
        decoded_path = urllib.parse.unquote(
            parsed.path[len(SELF_GITHUB_PATH_PREFIX) :]
        )
        candidate = (root / decoded_path).resolve(strict=False)
    else:
        if parsed.scheme or parsed.netloc or raw_target.startswith("//"):
            return
        if parsed.path:
            decoded_path = urllib.parse.unquote(parsed.path)
            candidate = (markdown.parent / decoded_path).resolve(strict=False)
        elif parsed.fragment:
            candidate = markdown
        else:
            return
    try:
        relative_candidate = candidate.relative_to(root)
    except ValueError:
        tree_kind = "source tree" if source_tree else "artifact docs"
        failures.append(f"{markdown.relative_to(root)}: link escapes {tree_kind}: {raw_target}")
        return
    if source_tree and not self_github_link and source_path_is_excluded(relative_candidate):
        return
    if not candidate.exists():
        failures.append(f"{markdown.relative_to(root)}: missing local link target: {raw_target}")
        return
    if parsed.fragment and candidate.is_file() and candidate.suffix.lower() == ".md":
        fragment = urllib.parse.unquote(parsed.fragment)
        anchors = anchor_cache.get(candidate)
        if anchors is None:
            anchors = markdown_anchors(candidate.read_text(encoding="utf-8"))
            anchor_cache[candidate] = anchors
        if fragment not in anchors:
            failures.append(
                f"{markdown.relative_to(root)}: missing local Markdown anchor: "
                f"{candidate.relative_to(root)}#{fragment}"
            )


def reference_definitions(text: str) -> tuple[dict[str, str], str]:
    definitions: dict[str, str] = {}
    definition_spans: list[tuple[int, int]] = []
    for match in REFERENCE_DEFINITION_RE.finditer(text):
        label = normalize_reference_label(match.group(1))
        if label:
            # CommonMark resolves duplicate labels using the first definition.
            target = match.group(2) if match.group(2) is not None else match.group(3)
            definitions.setdefault(label, target)
        line_end = text.find("\n", match.end())
        definition_spans.append((match.start(), len(text) if line_end == -1 else line_end))

    # Do not mistake bracket pairs in a definition's destination or title for
    # usages. Preserve newlines so later diagnostics still refer to the source.
    characters = list(text)
    for start, end in definition_spans:
        for index in range(start, end):
            if characters[index] != "\n":
                characters[index] = " "
    return definitions, "".join(characters)


def verify(root: pathlib.Path, *, source_tree: bool) -> tuple[list[str], int]:
    failures: list[str] = []
    anchor_cache: dict[pathlib.Path, frozenset[str]] = {}
    markdown_files = source_markdown_files(root) if source_tree else sorted(root.rglob("*.md"))
    if not markdown_files:
        tree_kind = "source" if source_tree else "artifact documentation"
        failures.append(f"{tree_kind} tree contains no Markdown files")

    for markdown in markdown_files:
        text = markdown_without_fenced_code(markdown.read_text(encoding="utf-8"))
        for match in LINK_RE.finditer(text):
            validate_link(
                match.group(1).strip("<>"),
                markdown,
                root,
                source_tree=source_tree,
                anchor_cache=anchor_cache,
                failures=failures,
            )

        definitions, text_without_definitions = reference_definitions(text)
        for match in REFERENCE_USAGE_RE.finditer(text_without_definitions):
            raw_label = match.group(2) or match.group(1)
            label = normalize_reference_label(raw_label)
            target = definitions.get(label)
            if target is None:
                failures.append(
                    f"{markdown.relative_to(root)}: undefined reference link label: {raw_label}"
                )
                continue
            validate_link(
                target,
                markdown,
                root,
                source_tree=source_tree,
                anchor_cache=anchor_cache,
                failures=failures,
            )

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
