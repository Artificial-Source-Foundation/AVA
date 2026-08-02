#!/usr/bin/env python3

# Run this script as
#
# scripts/find-unlabelled-namespace-closes.py
#
# to print all occurrences of namespace closing braces not matching the CLOSE_NAMESPACE_RE.

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


OPEN_NAMESPACE_RE = re.compile(r"namespace(?: [A-Za-z_:]+)? \{")
CLOSE_NAMESPACE_RE = re.compile(r"}\s+//\s+namespace(?: [A-Za-z_:]+)?")


@dataclass(frozen=True)
class Namespace:
  line: int
  text: str


def source_files(paths: list[Path]):
  for path in paths:
    if path.is_dir():
      yield from sorted(
          candidate for candidate in path.rglob("*")
          if candidate.is_file() and candidate.suffix in {".cpp", ".h"})
    elif path.is_file():
      yield path
    else:
      print(f"{path}: No such file or directory.", file=sys.stderr)


def namespace_openings(text: str) -> dict[int, Namespace]:
  openings = {}
  offset = 0

  for line_number, line in enumerate(text.splitlines(keepends=True), 1):
    logical_line = line.removesuffix("\n")
    if OPEN_NAMESPACE_RE.fullmatch(logical_line):
      openings[offset + logical_line.index("{")] = Namespace(
          line_number, logical_line)
    offset += len(line)

  return openings


def brace_tokens(text: str):
  """Yield (offset, line, brace) for braces that are C++ lexical tokens."""
  i = 0
  line = 1
  size = len(text)
  beginning_of_line = True

  while i < size:
    ch = text[i]

    if ch == "\n":
      line += 1
      beginning_of_line = True
      i += 1
      continue

    if beginning_of_line and ch in " \t\v\f\r":
      i += 1
      continue

    if beginning_of_line and ch == "#":
      # A preprocessing directive can contain brace characters in a macro.
      while i < size:
        newline = text.find("\n", i)
        if newline == -1:
          return
        line += 1
        if newline == 0 or text[newline - 1] != "\\":
          i = newline + 1
          beginning_of_line = True
          break
        i = newline + 1
      continue

    beginning_of_line = False

    if ch.isdigit() or (
        ch == "." and i + 1 < size and text[i + 1].isdigit()):
      # Consume a preprocessing number, including C++ digit separators.
      i += 1
      while i < size:
        ch = text[i]
        if ch.isalnum() or ch in "_.":
          i += 1
        elif ch == "'" and i + 1 < size and (
            text[i + 1].isalnum() or text[i + 1] == "_"):
          i += 2
        elif ch in "+-" and text[i - 1] in "eEpP":
          i += 1
        else:
          break
      continue

    if text.startswith("//", i):
      while True:
        newline = text.find("\n", i + 2)
        if newline == -1:
          return
        line += 1
        if newline == 0 or text[newline - 1] != "\\":
          i = newline + 1
          beginning_of_line = True
          break
        i = newline + 1
      continue

    if text.startswith("/*", i):
      end = text.find("*/", i + 2)
      if end == -1:
        raise ValueError(f"unterminated block comment beginning on line {line}")
      line += text.count("\n", i, end + 2)
      beginning_of_line = end > i and text[end - 1] == "\n"
      i = end + 2
      continue

    if ch == "R" and i + 1 < size and text[i + 1] == '"':
      delimiter_end = text.find("(", i + 2, min(i + 19, size))
      if delimiter_end != -1:
        delimiter = text[i + 2:delimiter_end]
        if not any(c in " ()\\\t\v\f\r\n" for c in delimiter):
          terminator = ")" + delimiter + '"'
          end = text.find(terminator, delimiter_end + 1)
          if end == -1:
            raise ValueError(f"unterminated raw string beginning on line {line}")
          end += len(terminator)
          line += text.count("\n", i, end)
          beginning_of_line = end > i and text[end - 1] == "\n"
          i = end
          continue

    if ch in "\"'":
      quote = ch
      start_line = line
      i += 1
      while i < size:
        if text[i] == "\\":
          if i + 1 < size and text[i + 1] == "\n":
            line += 1
          i += 2
        elif text[i] == quote:
          i += 1
          break
        elif text[i] == "\n":
          raise ValueError(
              f"unterminated {quote} literal beginning on line {start_line}")
        else:
          i += 1
      else:
        raise ValueError(
            f"unterminated {quote} literal beginning on line {start_line}")
      continue

    if ch in "{}":
      yield i, line, ch

    i += 1


def check_file(path: Path) -> bool:
  try:
    text = path.read_text(encoding="utf-8")
  except (OSError, UnicodeError) as error:
    print(f"{path}: {error}", file=sys.stderr)
    return False

  openings = namespace_openings(text)
  lines = text.splitlines()
  stack: list[Namespace | None] = []

  try:
    for offset, line_number, brace in brace_tokens(text):
      if brace == "{":
        stack.append(openings.pop(offset, None))
        continue

      if not stack:
        print(f"{path}:{line_number}: unmatched closing brace.", file=sys.stderr)
        return False

      namespace = stack.pop()
      if namespace is not None and not CLOSE_NAMESPACE_RE.fullmatch(
          lines[line_number - 1]):
        print(
            f"{path}:{line_number}: closes namespace from line "
            f"{namespace.line}: {lines[line_number - 1]}")

  except ValueError as error:
    print(f"{path}: {error}.", file=sys.stderr)
    return False

  if stack:
    print(f"{path}: {len(stack)} unmatched opening brace(s).", file=sys.stderr)
    return False
  if openings:
    line_numbers = ", ".join(str(item.line) for item in openings.values())
    print(
        f"{path}: namespace opening(s) on line(s) {line_numbers} were not tokens.",
        file=sys.stderr)
    return False
  return True


def main() -> int:
  parser = argparse.ArgumentParser(
      description="Find namespace closing braces without namespace comments.")
  parser.add_argument(
      "paths", nargs="*", type=Path, default=[Path("src/ava")],
      help="Files or directories to scan (default: src/ava).")
  args = parser.parse_args()

  valid = True
  seen: set[Path] = set()
  for path in source_files(args.paths):
    if path not in seen:
      valid = check_file(path) and valid
      seen.add(path)

  return 0 if valid else 1


if __name__ == "__main__":
  sys.exit(main())
