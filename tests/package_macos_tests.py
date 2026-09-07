#!/usr/bin/env python3
"""Exercise the real macOS archive and its relocated runtime, without user state."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=pathlib.Path, required=True)
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    parser.add_argument("--cmake", required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()
    build = args.build_dir.resolve()
    spec = importlib.util.spec_from_file_location("ava_package_macos", repo / "scripts/package-macos.py")
    package = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package)
    with tempfile.TemporaryDirectory(prefix="ava-package-macos-tests-") as temporary:
        root = pathlib.Path(temporary)
        env = package.isolated_environment(root / "environment")
        fixture = root / "executable"
        fixture.write_bytes(b"synthetic executable input")
        fixture.chmod(0o755)
        package.snapshot(fixture, root / "snapshot")
        if (root / "snapshot").read_bytes() != fixture.read_bytes():
            raise RuntimeError("snapshot changed input bytes")
        symlink = root / "symlink"
        symlink.symlink_to(fixture)
        fifo = root / "fifo"
        os.mkfifo(fifo)
        for source in (symlink, fifo):
            try:
                package.snapshot(source, root / "rejected")
            except (OSError, RuntimeError):
                pass
            else:
                raise RuntimeError("non-regular package input was accepted")
            if (root / "rejected").exists():
                raise RuntimeError("rejected input left an output snapshot")

        output = root / "artifact"
        command = [sys.executable, str(repo / "scripts/package-macos.py"), "--build-dir", str(build),
                   "--output-dir", str(output), "--cmake", args.cmake]
        package.run(command)
        archives = list(output.glob("*.tar.gz"))
        if len(archives) != 1:
            raise RuntimeError("packaging did not produce exactly one archive")
        archive = archives[0]
        checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
        if archive.with_name(archive.name + ".sha256").read_text() != f"{checksum}  {archive.name}\n":
            raise RuntimeError("archive checksum does not match")
        duplicate = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
        if duplicate.returncode == 0 or hashlib.sha256(archive.read_bytes()).hexdigest() != checksum:
            raise RuntimeError("packaging overwrote an existing output")
        relocated = root / "relocated path with spaces"
        relocated.mkdir()
        with tarfile.open(archive, "r:gz") as tar:
            if any(not (member.isfile() or member.isdir()) for member in tar.getmembers()):
                raise RuntimeError("archive contains links or special files")
        package.extract_archive(archive, relocated)
        stage = next(relocated.iterdir())
        binary = stage / "bin/ava"
        provenance = json.loads((stage / "share/doc/ava/PROVENANCE.json").read_text())
        if provenance["release_qualified"] or provenance["notarized"] or provenance["signing"] != "ad-hoc":
            raise RuntimeError("development archive overstates release qualification")
        if package.digest(binary) != provenance["binary_sha256"]:
            raise RuntimeError("provenance does not identify the packaged binary")
        for library in provenance["libraries"]:
            if package.digest(stage / library["file"]) != library["sha256"]:
                raise RuntimeError("provenance does not identify a bundled library")
        package.run(["/usr/bin/codesign", "--verify", "--strict", str(binary)])
        loader = subprocess.run([str(binary), "--version"], env={**env, "DYLD_PRINT_LIBRARIES": "1"}, cwd=root,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10, check=True)
        loaded = loader.stderr
        if "/opt/homebrew/" in loaded or "/usr/local/" in loaded:
            raise RuntimeError("relocated archive loaded a Homebrew runtime dependency")
        if str(stage / "lib/") not in loaded:
            raise RuntimeError("loader did not confirm use of the bundled ncurses runtime")
        # This term name exists only in the relocated package. The real TTY smoke
        # must discover the bundled database without a TERMINFO environment hint.
        terminfo = stage / "share/terminfo"
        xterm = next(terminfo.rglob("xterm-256color"))
        custom = terminfo / "61/ava-package-test"
        custom.parent.mkdir(exist_ok=True)
        shutil.copyfile(xterm, custom)
        terminal_env = {**env, "AVA_TUI_OSC8_SMOKE": "1"}
        smoke = package.run([sys.executable, str(repo / "tests/tui_osc8_smoke.py"),
                             "--ava", str(binary), "--fake-provider", str(build / "tests/ava_fake_provider_server"),
                             "--root", str(root / "tui-osc8-smoke"), "--term", "ava-package-test"], env=terminal_env)
        if "osc8+osc52:" not in smoke:
            raise RuntimeError("relocated terminal/model/clipboard smoke did not complete")
        print("macOS archive: input guards, checksum, signatures, dylib relocation, bundled terminfo, fake-provider TTY, Markdown copy passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
