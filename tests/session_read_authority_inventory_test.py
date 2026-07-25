#!/usr/bin/env python3
"""Grep inventory for runtime session-history authority boundaries.

Intentional pathname reads are limited to observational/noncurrent surfaces:
  * SessionStore listing inspection;
  * session-tree metadata for stores opened only to list the tree;
  * the legacy load_session_metadata(SessionStore) compatibility adapter used by
    that tree builder; and
  * the unused generic project_transcript_bounded(SessionStore) compatibility
    adapter, which obtains one bounded vector before logical projection.

Current runtime, provider, compaction, permission, and RPC code must instead
consume SessionReadAuthority. An authority carries the
resolved SessionReadLimits policy selected for its runtime session, so ordinary
load() calls remain bounded after the history grows.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


def grep(source: pathlib.Path, pattern: str, paths: list[str]) -> list[str]:
    command = ["grep", "-RInE", "--include=*.cpp", "--include=*.h", pattern, *paths]
    completed = subprocess.run(command, cwd=source, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if completed.returncode not in (0, 1):
        raise RuntimeError(f"grep failed ({completed.returncode}): {completed.stderr.strip()}")
    return [line for line in completed.stdout.splitlines() if line]


def require_text(source: pathlib.Path, relative: str, needle: str, failures: list[str]) -> None:
    text = (source / relative).read_text(encoding="utf-8")
    if needle not in text:
        failures.append(f"missing authority boundary in {relative}: {needle}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    args = parser.parse_args()
    source = args.source.absolute()
    failures: list[str] = []

    direct_read_pattern = (
        r"([[:alnum:]_]*store|[.]store)[.]"
        r"(load|load_bounded|inspect_bounded|visit_entries|visit_entries_leased)[[:space:]]*[(]"
    )
    runtime_paths = ["src/ava/app", "src/ava/agent"]
    direct_runtime_reads = grep(source, direct_read_pattern, runtime_paths)
    if direct_runtime_reads:
        failures.append("authority-sensitive runtime code still performs direct SessionStore history reads:\n  " + "\n  ".join(direct_runtime_reads))

    legacy_metadata_reads = grep(source, r"load_session_metadata[[:space:]]*[(]", runtime_paths)
    if legacy_metadata_reads:
        failures.append("runtime code still uses pathname/lease metadata adapters instead of SessionReadAuthority:\n  " + "\n  ".join(legacy_metadata_reads))

    require_text(source, "src/ava/agent/message_builder.h", "build_messages(ava::session::SessionReadAuthority read_authority", failures)
    require_text(source, "src/ava/app/runtime_compaction.h", "compact_runtime_context(Session& session, ava::session::SessionReadAuthority read_authority", failures)
    require_text(source, "src/ava/agent/agent_loop.h", "std::optional<ava::session::SessionReadAuthority> session_read_authority", failures)
    require_text(source, "src/ava/app/runtime/Session.h", "create_ephemeral(store, session_read_limits)", failures)
    require_text(source, "src/ava/app/runtime/Session.h", "create_persistent(store, lease, session_read_limits)", failures)
    require_text(source, "src/ava/session/session_store.cpp", "state_->store.load_bounded(*state_->lease, state_->limits)", failures)
    require_text(source, "src/ava/session/session_store.cpp", "state_->store.load_bounded(state_->limits)", failures)

    # Keep the small observational pathname inventory visible and intentional.
    require_text(source, "src/ava/session/session_tree.cpp", "load_session_metadata(*store)", failures)
    require_text(source, "src/ava/session/session_metadata.cpp", "auto entries = store.load();", failures)
    require_text(source, "src/ava/session/transcript.cpp", "auto entries = store.load_bounded(", failures)
    require_text(source, "src/ava/session/session_store_read.cpp", "inspect_bounded_for_listing", failures)

    if failures:
        print("\n\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
