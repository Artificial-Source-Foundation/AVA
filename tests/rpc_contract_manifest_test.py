#!/usr/bin/env python3
"""Validate checked AVA RPC v1 fixtures and catalog drift."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import subprocess
import tempfile


def fail(message: str) -> None:
    raise SystemExit(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--ava", required=True, type=pathlib.Path)
    args = parser.parse_args()
    root = args.source.absolute()
    golden = root / "tests/golden/rpc-v1"
    manifest = json.loads((golden / "manifest.json").read_text(encoding="utf-8"))

    requests = manifest["request_types"]
    events = manifest["event_names"]
    codes = manifest["stable_error_codes"]
    for name, values in (("request_types", requests), ("event_names", events), ("stable_error_codes", codes)):
        if len(values) != len(set(values)):
            fail(f"duplicate {name} in RPC manifest")

    for fixture in manifest["fixtures"]:
        path = golden / fixture
        if not path.is_file():
            fail(f"missing RPC fixture: {fixture}")
        if path.suffix == ".jsonl":
            for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                try:
                    value = json.loads(line)
                except json.JSONDecodeError as error:
                    fail(f"{fixture}:{line_number}: {error}")
                if not isinstance(value, dict):
                    fail(f"{fixture}:{line_number}: expected object")
        else:
            json.loads(path.read_text(encoding="utf-8"))

    docs = (root / "docs/rpc-protocol.md").read_text(encoding="utf-8")
    catalog = docs.split("<!-- command-catalog:start -->", 1)[1].split("<!-- command-catalog:end -->", 1)[0]
    documented = set(re.findall(r"^\| `([a-z0-9_]+)` \|", catalog, re.MULTILINE)) - {"type"}
    if set(requests) != documented:
        fail(f"RPC command catalog drift: undocumented={sorted(set(requests) - documented)}, extra={sorted(documented - set(requests))}")
    for event in events:
        if f"`{event}`" not in docs:
            fail(f"RPC event missing from normative docs: {event}")
    for code in codes:
        if f"`{code}`" not in docs:
            fail(f"RPC error code missing from normative docs: {code}")

    framing = json.loads((golden / "framing.json").read_text(encoding="utf-8"))
    if framing["encoding"] != "UTF-8" or framing["delimiter_hex"] != "0a":
        fail("RPC framing fixture must freeze UTF-8 and LF")

    with tempfile.TemporaryDirectory(prefix="ava-rpc-contract-") as temporary:
        temp = pathlib.Path(temporary)
        env = os.environ.copy()
        env.update(
            {
                "HOME": str(temp / "home"),
                "XDG_CONFIG_HOME": str(temp / "config"),
                "XDG_STATE_HOME": str(temp / "state"),
                "XDG_DATA_HOME": str(temp / "data"),
                "NO_COLOR": "1",
            }
        )
        workspace = temp / "workspace"
        workspace.mkdir(parents=True)

        wire_lines = (golden / "wire.jsonl").read_bytes().splitlines(keepends=True)
        wire = subprocess.run(
            [str(args.ava), "--rpc", "--offline"],
            input=wire_lines[0],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=workspace,
            env=env,
            check=False,
            timeout=5,
        )
        if wire.returncode != 0 or wire.stdout != wire_lines[1]:
            fail(f"real get_protocol wire output drifted: status={wire.returncode} stdout={wire.stdout!r} stderr={wire.stderr!r}")

        for case in framing["cases"]:
            run = subprocess.run(
                [str(args.ava), "--rpc", "--offline"],
                input=bytes.fromhex(case["input_hex"]),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=workspace,
                env=env,
                check=False,
                timeout=5,
            )
            try:
                decoded = run.stdout.decode("utf-8", errors="strict")
                records = [json.loads(line) for line in decoded.splitlines()]
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                fail(f"real framing case {case['name']} emitted invalid UTF-8/JSON: {error}")
            expected_record_count = len(case.get("records", [""]))
            if run.returncode != 0 or len(records) != expected_record_count:
                fail(f"real framing case {case['name']} drifted: status={run.returncode} records={records!r} stderr={run.stderr!r}")
            if any(record.get("id") != "" or record.get("error", {}).get("code") != "invalid_request" for record in records):
                fail(f"real framing case {case['name']} did not recover with empty-id invalid_request: {records!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
