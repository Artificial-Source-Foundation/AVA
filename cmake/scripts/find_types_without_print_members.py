#!/usr/bin/env python3
# find_types_without_print_members.py -- report ava header types missing AVA_DEBUG_PRINT_MEMBERS_ON.
#
# Reads the JSON tags file ($BUILDDIR/generated/ctags/tags.json) and lists every
# struct and class defined in a header under src/ava/ that does not yet declare
# the AVA_DEBUG_PRINT_MEMBERS_ON or AVA_DEBUG_PURE_VIRTUAL_PRINT_MEMBERS opt-in
# marker, nor the AVA_DEBUG_PRINT_MEMBERS_OPT_OUT marker.
#
# How opt-in/out is detected: the ctags invocation that produced tags.json passed
# `-D AVA_DEBUG_PRINT_MEMBERS_ON=void print_members_opt_in() { }`, so every
# class/struct that has either macro in its body shows a `print_members_opt_in`
# or `print_members_opt_out` member whose scope is the type's fully-qualified name.
#
# A type is therefore considered to addressed when its fully-qualified name appears as
# the scope of such a print_members_opt_in / print_members_opt_out member respectively.
#
# This mirrors the detection logic in generate_print_members.py.
#
# Usage: find_types_without_print_members.py <tags.json>
import argparse
import json
import re
import sys
from collections import defaultdict

HEADER_EXTS = (".h", ".hpp", ".hxx", ".hh")
AVA_HEADER_PREFIX = "src/ava/"
# Headers under src/ava/debug/ implement the debug-printing infrastructure
# itself (the macros, ostream operators, print_reference helpers, etc.). They
# are not application types and are not expected to carry AVA_DEBUG_PRINT_MEMBERS_ON,
# so they are excluded from the scan.
AVA_DEBUG_EXCLUDE_PREFIX = "src/ava/debug/"
# Types that are intentionally exempt from the AVA_DEBUG_PRINT_MEMBERS_ON
# requirement and must never be reported by this scan, keyed by fully-qualified
# name. These are typically tiny RAII/utility helpers (e.g. deleters or trivial
# value types) where debug-print support adds no value. Add a type here only when
# it has been deliberately reviewed and accepted without debug printing.
EXEMPT_TYPES = {
    "ava::tui::CursesSession::ScreenDeleter",
    "ava::tui::NewLine",
}


def is_real_definition(tag):
    # ctags sometimes tags a C-style variable declaration like
    # `struct sigaction previous_{};` as a struct named `previous_`. In a real
    # struct/class definition the first identifier after the `struct`/`class`
    # keyword is the type's own name; in such a variable declaration it is the
    # referenced type (here `sigaction`) and the tag name is the variable. This
    # returns False for those spurious tags so they are not reported. When the
    # pattern cannot be parsed the tag is kept (over-reporting is safer than
    # silently dropping a real definition).
    kind = tag.get("kind")  # "struct" or "class"
    name = tag.get("name", "")
    pattern = tag.get("pattern", "")
    inner = pattern
    if inner.startswith("/^"):
        inner = inner[2:]
    if inner.endswith("$/"):
        inner = inner[:-2]
    elif inner.endswith("/"):
        inner = inner[:-1]
    m = re.search(r"\b" + re.escape(kind) + r"\s+(\w+)", inner)
    return not m or m.group(1) == name


def load_tags(tags_path):
    # classes: fqname -> tag for every class/struct with a name and a path.
    # opted:   set of fqnames that declared print_members_opt_in (i.e. have
    #          AVA_DEBUG_PRINT_MEMBERS_ON in their body).
    classes = {}
    opted = set()
    with open(tags_path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            tag = json.loads(line)
            kind = tag.get("kind")
            name = tag.get("name")
            scope = tag.get("scope") or ""
            fqname = (scope + "::" + name) if (scope and name) else (name or "")
            if kind in ("class", "struct") and name and tag.get("path") and fqname:
                classes[fqname] = tag
            if (name == "print_members_opt_in" or name == "print_members_opt_out") and scope:
                opted.add(scope)
    return classes, opted


def is_ava_header(path):
    if not path.startswith(AVA_HEADER_PREFIX) or not path.endswith(HEADER_EXTS):
        return False
    # Skip the debug-printing infrastructure subtree.
    if path.startswith(AVA_DEBUG_EXCLUDE_PREFIX):
        return False
    return True


def in_anon_namespace(scope):
    return "__anon" in scope


def collect_missing(classes, opted, kinds):
    # Returns {path: [(line, fqname, scope, name)]} for types in src/ava/ headers
    # that are missing the opt-in marker, sorted by path then line.
    by_file = defaultdict(list)
    for fqname, tag in classes.items():
        if tag.get("kind") not in kinds:
            continue
        path = tag.get("path", "")
        if not is_ava_header(path):
            continue
        if fqname in opted:
            continue
        if fqname in EXEMPT_TYPES:
            continue
        if not is_real_definition(tag):
            continue
        line = tag.get("line", 0)
        name = tag["name"]
        scope = tag.get("scope") or ""
        by_file[path].append((line, fqname, scope, name))
    for path in by_file:
        by_file[path].sort(key=lambda t: t[0])
    return by_file


def report(title, by_file, out):
    out.write(title + "\n")
    total = sum(len(v) for v in by_file.values())
    if total == 0:
        out.write("  (none)\n")
        return total
    for path in sorted(by_file):
        entries = by_file[path]
        out.write("  " + path + " (" + str(len(entries)) + "):\n")
        for line, fqname, scope, name in entries:
            marker = " [anonymous namespace]" if in_anon_namespace(scope) else ""
            out.write("    line %5d  %s%s\n" % (line, fqname, marker))
    out.write("\n  Total: %d type(s) across %d header file(s).\n" %
              (total, len(by_file)))
    return total


def main(argv):
    parser = argparse.ArgumentParser(
        description="List ava header structs and classes missing "
                    "AVA_DEBUG_PRINT_MEMBERS_ON.")
    parser.add_argument("tags_json", help="Path to the ctags tags.json file.")
    args = parser.parse_args()

    classes, opted = load_tags(args.tags_json)

    struct_files = collect_missing(classes, opted, {"struct"})
    struct_total = report("Structs in src/ava/ headers WITHOUT "
                          "AVA_DEBUG_PRINT_MEMBERS_ON or AVA_DEBUG_PRINT_MEMBERS_OPT_OUT:", struct_files, sys.stdout)

    print()
    class_files = collect_missing(classes, opted, {"class"})
    class_total = report("Classes in src/ava/ headers WITHOUT "
                         "AVA_DEBUG_PRINT_MEMBERS_ON, AVA_DEBUG_PURE_VIRTUAL_PRINT_MEMBERS "
                         "or AVA_DEBUG_PRINT_MEMBERS_OPT_OUT:", class_files, sys.stdout)

    sys.stderr.write("\nSummary: %d struct(s), %d class(es) missing an opt-in or "
                     "opt-out marker.\n" % (struct_total, class_total))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
