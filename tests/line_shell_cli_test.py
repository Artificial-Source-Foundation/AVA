#!/usr/bin/env python3
"""Credential-free CLI selection coverage for AVA's line shell."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys


ENVIRONMENT_ALLOWLIST = (
    "PATH",
    "LANG",
    "LC_ALL",
    "LC_CTYPE",
    "LD_LIBRARY_PATH",
    "DYLD_LIBRARY_PATH",
    "DYLD_FALLBACK_LIBRARY_PATH",
    "ASAN_OPTIONS",
    "UBSAN_OPTIONS",
    "LSAN_OPTIONS",
    "TSAN_OPTIONS",
    "MSAN_OPTIONS",
    "ASAN_SYMBOLIZER_PATH",
    "LLVM_SYMBOLIZER_PATH",
    "TMPDIR",
    "TZ",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def isolated_environment(root: pathlib.Path) -> dict[str, str]:
    environment = {name: os.environ[name] for name in ENVIRONMENT_ALLOWLIST if name in os.environ}
    directories = {
        "HOME": root / "home",
        "XDG_CONFIG_HOME": root / "config",
        "XDG_STATE_HOME": root / "state",
        "XDG_DATA_HOME": root / "data",
        "XDG_CACHE_HOME": root / "cache",
        "XDG_RUNTIME_DIR": root / "runtime",
    }
    for name, directory in directories.items():
        directory.mkdir(parents=True, exist_ok=True)
        environment[name] = str(directory)
    directories["XDG_RUNTIME_DIR"].chmod(0o700)
    libcwd_rcfile = root / "libcwdrc"
    libcwd_rcfile.write_text("silent = on\nchannels_default = off\n", encoding="utf-8")
    libcwd_rcfile.chmod(0o600)
    environment.update(
        {
            "TERM": "dumb",
            "NO_COLOR": "1",
            # Debug builds must remain CLI-quiet when the isolated HOME has no
            # developer libcwd configuration.
            "LIBCWD_RCFILE_NAME": str(libcwd_rcfile),
        }
    )
    return environment


def run(ava: pathlib.Path, arguments: list[str], environment: dict[str, str], workspace: pathlib.Path, input_bytes: bytes = b"") -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        [str(ava), *arguments],
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=workspace,
        env=environment,
        timeout=15,
        check=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ava", required=True)
    parser.add_argument("--root", required=True)
    args = parser.parse_args()

    ava = pathlib.Path(args.ava).resolve()
    require(ava.is_file(), f"AVA executable does not exist: {ava}")
    root = pathlib.Path(args.root).resolve()
    if root.exists():
        require(root.name == "line-shell-cli", f"refusing to clear unexpected test root: {root}")
        shutil.rmtree(root)
    root.mkdir(parents=True, mode=0o700)
    workspace = root / "workspace"
    workspace.mkdir()
    environment = isolated_environment(root)

    help_result = run(ava, ["--help"], environment, workspace)
    require(help_result.returncode == 0 and b"--line-shell" in help_result.stdout, "--help does not advertise --line-shell")

    combinations = (
        (["--line-shell", "--print"], b"use either --line-shell or --print, not both"),
        (["--line-shell", "--rpc"], b"use either --line-shell or --rpc, not both"),
        (["--line-shell", "--acp"], b"use either --line-shell or --acp, not both"),
    )
    for arguments, expected_error in combinations:
        result = run(ava, arguments, environment, workspace)
        require(
            result.returncode == 2 and result.stdout == b"" and expected_error in result.stderr,
            f"incompatible arguments {arguments!r} were not rejected actionably: rc={result.returncode} stdout={result.stdout!r} stderr={result.stderr!r}",
        )

    explicit = run(ava, ["--line-shell", "--offline", "--no-session"], environment, workspace, b"/exit\n")
    require(
        explicit.returncode == 0
        and explicit.stderr == b""
        and b"line shell" in explicit.stdout
        and b"[build] ava>" in explicit.stdout
        and b"Session history was not saved" in explicit.stdout,
        f"explicit line shell did not parse and exit cleanly: rc={explicit.returncode} stdout={explicit.stdout!r} stderr={explicit.stderr!r}",
    )

    fallback = run(ava, ["--offline", "--no-session"], environment, workspace, b"/exit\n")
    require(
        fallback.returncode == 0 and fallback.stderr == b"" and b"line shell" in fallback.stdout and b"[build] ava>" in fallback.stdout,
        f"non-TTY automatic line-shell fallback changed: rc={fallback.returncode} stdout={fallback.stdout!r} stderr={fallback.stderr!r}",
    )

    cursor = run(ava, ["--line-shell", "--offline", "--no-session"], environment, workspace, b"/cursor bar steady\n/exit\n")
    display_path = pathlib.Path(environment["XDG_CONFIG_HOME"]) / "ava" / "display.json"
    display = json.loads(display_path.read_text(encoding="utf-8")) if display_path.is_file() else {}
    require(
        cursor.returncode == 0
        and cursor.stderr == b""
        and b"Stored TUI cursor bar steady" in cursor.stdout
        and b"\x1b[" not in cursor.stdout
        and display.get("cursor_style") == "bar"
        and display.get("cursor_blink") is False,
        f"line-shell cursor persistence emitted a full-screen protocol or stored the wrong fields: "
        f"rc={cursor.returncode} stdout={cursor.stdout!r} stderr={cursor.stderr!r} display={display!r}",
    )

    at_limit_line = b"/unknown " + b"x" * (65536 - len(b"/unknown "))
    at_limit = run(ava, ["--line-shell", "--offline", "--no-session"], environment, workspace, at_limit_line + b"\n/exit\n")
    require(
        at_limit.returncode == 0 and b"65536-byte line limit" not in at_limit.stdout and b"Unknown command: /unknown" in at_limit.stdout,
        f"line shell rejected a submitted line at the 64 KiB limit: rc={at_limit.returncode} stdout={at_limit.stdout[-1000:]!r}",
    )

    over_limit = run(
        ava,
        ["--line-shell", "--offline", "--no-session"],
        environment,
        workspace,
        b"x" * 65537 + b"\n/exit\n",
    )
    require(
        over_limit.returncode == 0
        and b"65536-byte line limit" in over_limit.stdout
        and b"submitted line was cleared" in over_limit.stdout
        and over_limit.stdout.count(b"[build] ava>") >= 2,
        f"line shell did not reject, clear, and recover after an oversized line: rc={over_limit.returncode} stdout={over_limit.stdout!r}",
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
