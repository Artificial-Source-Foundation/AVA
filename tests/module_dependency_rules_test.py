#!/usr/bin/env python3
"""Check internal-module dependency direction and temporary exceptions.

Only the eight production modules named by the policy are scanned. The policy
is intentionally an exact include inventory rather than a module-pair waiver.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
import unittest

MODULES = ("config", "http", "provider", "session", "agent", "tools", "permissions", "app")
MODULE_SET = frozenset(MODULES)
ALLOWED = {
    "app": MODULE_SET - {"app"},
    "agent": frozenset({"config", "http", "provider", "session", "tools", "permissions"}),
    "provider": frozenset({"config", "http"}),
    "session": frozenset({"config"}),
    "tools": frozenset({"http", "permissions"}),
    "config": frozenset({"http"}),
    "http": frozenset(),
    "permissions": frozenset(),
}
SOURCE_SUFFIXES = frozenset({".h", ".hpp", ".cpp"})
IGNORED_TREE_NAMES = frozenset({"build", "generated", "reference", "tests", "vendor"})
INCLUDE = re.compile(r'^\s*#\s*include\s*(?:<([^>]+)>|"([^"]+)")')


class PolicyError(ValueError):
    pass


def source_kind(path: Path) -> str:
    return "implementation" if path.suffix == ".cpp" else "public"


def include_target(source: Path, including_path: Path, literal: str) -> str | None:
    if literal.startswith("ava/"):
        parts = PurePosixPath(literal).parts
        if PurePosixPath(literal).as_posix() != literal or "." in parts or ".." in parts or len(parts) < 3:
            return "<noncanonical>"
        return parts[1]

    # A relative include can otherwise bypass the canonical ava/<module>/ form.
    # Resolve it lexically against the including file and classify it whenever it
    # still names production code below src/ava.
    candidate = Path(*including_path.parent.parts, *PurePosixPath(literal).parts)
    normalized = Path(candidate.parent, candidate.name).resolve(strict=False)
    try:
        relative = normalized.relative_to((source / "src" / "ava").resolve())
    except ValueError:
        return None
    return relative.parts[0] if len(relative.parts) >= 2 else "<noncanonical>"


def collect_edges(source: Path) -> list[tuple[str, int, str, str, str, str]]:
    edges = []
    for module in MODULES:
        directory = source / "src" / "ava" / module
        if not directory.is_dir():
            continue
        for path in sorted(
            candidate
            for candidate in directory.rglob("*")
            if candidate.is_file() and candidate.suffix in SOURCE_SUFFIXES and not any(part in IGNORED_TREE_NAMES for part in candidate.relative_to(directory).parts)
        ):
            relative = path.relative_to(source).as_posix()
            for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                matched = INCLUDE.match(line)
                if not matched:
                    continue
                literal = matched.group(1) or matched.group(2)
                target = include_target(source, path, literal)
                if target is None or target == module or (target not in MODULE_SET and target != "<noncanonical>"):
                    continue
                if target == "<noncanonical>" or target not in ALLOWED[module]:
                    edges.append((relative, line_number, source_kind(path), module, target, literal))
    return sorted(edges)


def relative_source(value: object) -> str:
    if not isinstance(value, str) or not value:
        raise PolicyError("exception source must be a nonempty relative path")
    path = Path(value)
    if path.is_absolute() or ".." in path.parts or path.as_posix() != value or not value.startswith("src/ava/"):
        raise PolicyError(f"exception source is not a normalized relative production path: {value!r}")
    parts = path.parts
    if len(parts) < 4 or parts[2] not in MODULE_SET or path.suffix not in SOURCE_SUFFIXES:
        raise PolicyError(f"exception source has an unknown module or unsupported extension: {value!r}")
    return value


def exception_key(entry: object) -> tuple[str, str, str]:
    if not isinstance(entry, dict) or set(entry) != {"source", "include", "kind", "reason", "tracking"}:
        raise PolicyError("each exception must contain exactly source, include, kind, reason, and tracking")
    source = relative_source(entry["source"])
    include = entry["include"]
    kind = entry["kind"]
    reason = entry["reason"]
    tracking = entry["tracking"]
    include_parts = PurePosixPath(include).parts if isinstance(include, str) else ()
    if (
        not isinstance(include, str)
        or PurePosixPath(include).as_posix() != include
        or "." in include_parts
        or ".." in include_parts
        or len(include_parts) < 3
        or include_parts[0] != "ava"
        or include_parts[1] not in MODULE_SET
    ):
        raise PolicyError(f"exception include is not a canonical ava/<module>/... path with a known module: {include!r}")
    if kind not in {"public", "implementation"}:
        raise PolicyError(f"exception kind must be public or implementation: {kind!r}")
    if not isinstance(reason, str) or not reason.strip():
        raise PolicyError("exception reason must be nonempty")
    if not isinstance(tracking, str) or not tracking.strip():
        raise PolicyError("exception tracking must be nonempty")
    return source, include, kind


def load_policy(path: Path) -> set[tuple[str, str, str]]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PolicyError(f"cannot read policy {path}: {error}") from error
    if not isinstance(document, dict) or set(document) != {"exceptions"} or not isinstance(document["exceptions"], list):
        raise PolicyError("policy must be an object containing only an exceptions array")
    keys = set()
    duplicates = []
    for entry in document["exceptions"]:
        key = exception_key(entry)
        if key in keys:
            duplicates.append(key)
        keys.add(key)
    if duplicates:
        rendered = "\n".join(f"duplicate exception: {source} {kind} {include}" for source, include, kind in sorted(set(duplicates)))
        raise PolicyError(rendered)
    return keys


def check(source: Path, policy: Path) -> list[str]:
    exceptions = load_policy(policy)
    forbidden = collect_edges(source)
    scanned = {(path, include, kind) for path, _, kind, _, _, include in forbidden}
    diagnostics = [
        f"{path}:{line}: {kind} {module} -> {target} ({include})"
        for path, line, kind, module, target, include in forbidden
        if (path, include, kind) not in exceptions
    ]
    diagnostics.extend(
        f"stale exception (not a currently forbidden include): {path} {kind} {include}"
        for path, include, kind in sorted(exceptions - scanned)
    )
    return sorted(diagnostics)


class ModuleDependencyRulesSelfTest(unittest.TestCase):
    def run_check(self, source_text: str, exceptions: list[dict], module: str = "config") -> list[str]:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "src" / "ava" / module / "fixture.cpp"
            source.parent.mkdir(parents=True)
            source.write_text(source_text, encoding="utf-8")
            policy = root / "policy.json"
            policy.write_text(json.dumps({"exceptions": exceptions}), encoding="utf-8")
            return check(root, policy)

    def test_allowed_edge(self) -> None:
        self.assertEqual(self.run_check('#include "ava/config/model_config.h"\n', [], module="provider"), [])

    def test_config_and_tools_may_depend_on_http(self) -> None:
        self.assertEqual(self.run_check('#include "ava/http/transport.h"\n', [], module="config"), [])
        self.assertEqual(self.run_check('#include "ava/http/transport.h"\n', [], module="tools"), [])

    def test_http_cannot_depend_on_higher_layers(self) -> None:
        for target in ("provider", "config", "tools", "agent", "app"):
            with self.subTest(target=target):
                self.assertEqual(
                    self.run_check(f'#include "ava/{target}/fixture.h"\n', [], module="http"),
                    [f"src/ava/http/fixture.cpp:1: implementation http -> {target} (ava/{target}/fixture.h)"],
                )

    def test_forbidden_edge(self) -> None:
        self.assertEqual(
            self.run_check('#include "ava/provider/provider.h"\n', []),
            ['src/ava/config/fixture.cpp:1: implementation config -> provider (ava/provider/provider.h)'],
        )

    def test_angle_bracket_forbidden_edge(self) -> None:
        self.assertEqual(
            self.run_check("#include <ava/provider/provider.h>\n", []),
            ["src/ava/config/fixture.cpp:1: implementation config -> provider (ava/provider/provider.h)"],
        )

    def test_relative_forbidden_edge(self) -> None:
        self.assertEqual(
            self.run_check('#include "../provider/provider.h"\n', []),
            ["src/ava/config/fixture.cpp:1: implementation config -> provider (../provider/provider.h)"],
        )

    def test_noncanonical_internal_include(self) -> None:
        self.assertEqual(
            self.run_check('#include "ava/provider/../unknown/header.h"\n', []),
            ["src/ava/config/fixture.cpp:1: implementation config -> <noncanonical> (ava/provider/../unknown/header.h)"],
        )

    def test_exact_exception(self) -> None:
        exception = {
            "source": "src/ava/config/fixture.cpp",
            "include": "ava/provider/provider.h",
            "kind": "implementation",
            "reason": "Temporary characterization fixture.",
            "tracking": "test-only",
        }
        self.assertEqual(self.run_check('#include "ava/provider/provider.h"\n', [exception]), [])

    def test_stale_exception(self) -> None:
        exception = {
            "source": "src/ava/config/fixture.cpp",
            "include": "ava/config/model_config.h",
            "kind": "implementation",
            "reason": "Temporary characterization fixture.",
            "tracking": "test-only",
        }
        self.assertEqual(
            self.run_check('#include "ava/config/model_config.h"\n', [exception]),
            ["stale exception (not a currently forbidden include): src/ava/config/fixture.cpp implementation ava/config/model_config.h"],
        )

    def test_policy_rejects_traversal_paths(self) -> None:
        include_traversal = {
            "source": "src/ava/config/fixture.cpp",
            "include": "ava/provider/../unknown/header.h",
            "kind": "implementation",
            "reason": "Invalid traversal fixture.",
            "tracking": "test-only",
        }
        with self.assertRaisesRegex(PolicyError, "canonical ava/<module>"):
            self.run_check('#include "ava/provider/provider.h"\n', [include_traversal])

        source_traversal = {
            "source": "src/ava/config/../provider/fixture.cpp",
            "include": "ava/provider/provider.h",
            "kind": "implementation",
            "reason": "Invalid traversal fixture.",
            "tracking": "test-only",
        }
        with self.assertRaisesRegex(PolicyError, "normalized relative production path"):
            self.run_check('#include "ava/provider/provider.h"\n', [source_traversal])

    def test_duplicate_exception(self) -> None:
        exception = {
            "source": "src/ava/config/fixture.cpp",
            "include": "ava/provider/provider.h",
            "kind": "implementation",
            "reason": "Temporary characterization fixture.",
            "tracking": "test-only",
        }
        with self.assertRaisesRegex(PolicyError, "duplicate exception"):
            self.run_check('#include "ava/provider/provider.h"\n', [exception, exception])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path)
    parser.add_argument("--policy", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(ModuleDependencyRulesSelfTest)
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    if args.source is None:
        parser.error("--source is required unless --self-test is used")
    source = args.source.resolve()
    policy = args.policy.resolve() if args.policy else source / "tests" / "fixtures" / "module_dependency_rules.json"
    try:
        diagnostics = check(source, policy)
    except PolicyError as error:
        print(f"module dependency policy error: {error}", file=sys.stderr)
        return 1
    if diagnostics:
        print("\n".join(diagnostics), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
