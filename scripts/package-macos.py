#!/usr/bin/env python3
"""Build a relocatable, ad-hoc-signed macOS archive from a configured AVA build."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile


REPO = pathlib.Path(__file__).resolve().parent.parent
SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/")
LIBRARY_PATTERN = re.compile(r"lib(?:ncursesw|formw|tinfo)\.[0-9.]+\.dylib\Z")


def run(argv: list[str], **kwargs: object) -> str:
    try:
        return subprocess.run(argv, check=True, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=120, **kwargs).stdout.strip()
    except subprocess.CalledProcessError as error:
        raise RuntimeError((error.stderr or error.stdout or str(error))[-4000:].strip()) from error


def dependencies(binary: pathlib.Path) -> list[str]:
    return [line.strip().split(" (compatibility version", 1)[0]
            for line in run(["/usr/bin/otool", "-L", str(binary)]).splitlines()[1:] if line.strip()]


def rpaths(binary: pathlib.Path) -> list[str]:
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+) \(offset \d+\)",
                      run(["/usr/bin/otool", "-l", str(binary)]))


def digest(path: pathlib.Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            result.update(chunk)
    return result.hexdigest()


def snapshot(source: pathlib.Path, target: pathlib.Path, *, executable: bool = True) -> None:
    # Darwin has no O_PATH. Open nonblocking without following the final link,
    # and check the descriptor before reading so FIFOs cannot stall packaging.
    descriptor = os.open(source, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        before = os.fstat(descriptor)
        if not stat.S_ISREG(before.st_mode) or (executable and not before.st_mode & 0o111):
            raise RuntimeError("package input must be a regular executable")
        if before.st_size > 256 * 1024 * 1024:
            raise RuntimeError("package executable exceeds the snapshot limit")
        with target.open("xb") as output:
            copied = 0
            while data := os.read(descriptor, 1024 * 1024):
                copied += len(data)
                if copied > before.st_size:
                    raise RuntimeError("package input grew during snapshot")
                output.write(data)
            output.flush()
            os.fsync(output.fileno())
        after = os.fstat(descriptor)
        identity = lambda value: (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns)
        if identity(before) != identity(after) or identity(after) != identity(source.stat(follow_symlinks=False)):
            target.unlink()
            raise RuntimeError("package input changed during snapshot")
        target.chmod(0o755 if executable else 0o644)
    finally:
        os.close(descriptor)


def bundle_libraries(binary: pathlib.Path, stage: pathlib.Path) -> list[dict[str, str]]:
    library_dir = stage / "lib"
    library_dir.mkdir(mode=0o755)
    pending = [binary]
    bundled: dict[str, pathlib.Path] = {}
    sources: dict[pathlib.Path, pathlib.Path] = {}
    notices: dict[pathlib.Path, pathlib.Path] = {}
    while pending:
        target = pending.pop()
        own_id = run(["/usr/bin/otool", "-D", str(target)]).splitlines()[1:]
        for dependency in dependencies(target):
            if dependency in own_id or dependency.startswith(SYSTEM_PREFIXES):
                continue
            if not dependency.startswith("/"):
                raise RuntimeError(f"unresolved runtime dependency: {dependency}")
            source_library = pathlib.Path(dependency).resolve(strict=True)
            name = source_library.name
            if not LIBRARY_PATTERN.fullmatch(name):
                raise RuntimeError(f"unreviewed bundled runtime library: {name}")
            existing = bundled.get(name)
            if existing and digest(existing) != digest(source_library):
                raise RuntimeError(f"conflicting runtime libraries named {name}")
            if not existing:
                copied = library_dir / name
                snapshot(source_library, copied, executable=False)
                bundled[name] = source_library
                sources[copied] = source_library
                pending.append(copied)
                license_file = source_library.parent.parent / "COPYING"
                if not license_file.is_file():
                    raise RuntimeError(f"bundled ncurses license is missing: {license_file}")
                notices[license_file] = source_library.parent.parent
            relative = f"@executable_path/../lib/{name}" if target == binary else f"@loader_path/{name}"
            run(["/usr/bin/install_name_tool", "-change", dependency, relative, str(target)])
        for path in rpaths(target):
            run(["/usr/bin/install_name_tool", "-delete_rpath", path, str(target)])
        if target != binary:
            run(["/usr/bin/install_name_tool", "-id", f"@loader_path/{target.name}", str(target)])
    license_dir = stage / "share/doc/ava/licenses"
    license_dir.mkdir(mode=0o755)
    if len(notices) > 1 and len({digest(path) for path in notices}) != 1:
        raise RuntimeError("bundled ncurses libraries have inconsistent license notices")
    for path in notices:
        shutil.copyfile(path, license_dir / "ncurses-COPYING")
    if notices:
        source_terminfo = next(iter(notices.values())) / "share/terminfo"
        source_terminfo = source_terminfo.resolve(strict=True)
        for path in source_terminfo.rglob("*"):
            if path.is_symlink() and not path.resolve(strict=True).is_relative_to(source_terminfo):
                raise RuntimeError("ncurses terminfo contains an external symlink")
        shutil.copytree(source_terminfo, stage / "share/terminfo", symlinks=False)
    # Retain only system dependencies and self-contained loader-relative edges.
    for target in [*sources, binary]:
        for dependency in dependencies(target):
            if dependency.startswith(SYSTEM_PREFIXES):
                continue
            if not dependency.startswith(("@loader_path/", "@executable_path/../lib/")):
                raise RuntimeError(f"archive still depends on a host path: {dependency}")
            if not (library_dir / pathlib.Path(dependency).name).is_file():
                raise RuntimeError(f"archive is missing runtime library: {dependency}")
        run(["/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none", str(target)])
        run(["/usr/bin/codesign", "--verify", "--strict", str(target)])
    return [{"file": f"lib/{name}", "source_sha256": digest(source),
             "sha256": digest(library_dir / name)} for name, source in sorted(bundled.items())]


def extract_archive(archive: pathlib.Path, destination: pathlib.Path) -> None:
    # Python 3.10 has no tarfile data filter. Materialize only regular files and
    # directories below a new private root, without restoring archive ownership.
    if any(destination.iterdir()):
        raise RuntimeError("archive extraction requires an empty destination")
    total_bytes = 0
    with tarfile.open(archive, "r:gz") as tar:
        for member in tar:
            parts = member.name.split("/")
            if not parts or any(part in {"", ".", ".."} for part in parts) or not (member.isdir() or member.isfile()):
                raise RuntimeError("archive contains an unsafe entry")
            total_bytes += member.size
            if total_bytes > 512 * 1024 * 1024:
                raise RuntimeError("archive exceeds the extraction limit")
            target = destination.joinpath(*parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True, mode=0o755)
            else:
                target.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
                with tar.extractfile(member) as source, target.open("xb") as output:
                    shutil.copyfileobj(source, output)
                target.chmod(member.mode & 0o777)


def isolated_environment(root: pathlib.Path) -> dict[str, str]:
    env = {"PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "LANG": "en_US.UTF-8", "TERM": "xterm-256color",
           "AVA_CLIPBOARD_BACKEND": "terminal", "AVA_NO_DEBUG_OUTPUT": "1", "LIBCWD_NO_STARTUP_MSGS": "1"}
    for key in ("HOME", "XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME", "TMPDIR"):
        target = root / key.lower()
        target.mkdir(parents=True, exist_ok=True, mode=0o700)
        env[key] = str(target)
    return env


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--output-dir", type=pathlib.Path,
                        help="new output directory outside the checkout; existing destinations are never overwritten")
    parser.add_argument("--cmake", default="cmake")
    args = parser.parse_args()
    if platform.system() != "Darwin":
        raise RuntimeError("macOS packaging requires a native Mac host")
    build = args.build_dir.resolve(strict=True)
    cache = (build / "CMakeCache.txt").read_text()
    if f"CMAKE_HOME_DIRECTORY:INTERNAL={REPO}\n" not in cache:
        raise RuntimeError("build directory must belong to this AVA checkout")
    if re.search(r"(?:EnableAvaSanitizers|AVA_ENABLE_SANITIZERS|AVA_ENABLE_TSAN):BOOL=ON", cache):
        raise RuntimeError("sanitizer builds are for testing; package the dev or release build")
    output = args.output_dir.absolute() if args.output_dir else pathlib.Path(tempfile.mkdtemp(prefix="ava-macos-artifacts-"))
    if output.resolve().is_relative_to(REPO):
        raise RuntimeError("package output must be outside the checkout")
    if args.output_dir and (output.exists() or output.is_symlink()):
        raise RuntimeError("package output directory already exists")
    with tempfile.TemporaryDirectory(prefix="ava-package-macos-") as temporary:
        work = pathlib.Path(temporary)
        original = work / "ava"
        snapshot(build / "ava", original)
        environment = isolated_environment(work / "smoke")
        version_output = run([str(original), "--version"], env=environment, cwd=work)
        match = re.fullmatch(r"ava (\d+\.\d+\.\d+)", version_output)
        if not match:
            raise RuntimeError("binary version did not match AVA's version contract")
        version = match.group(1)
        project_version = re.search(r"project\(ava\s+VERSION\s+(\d+\.\d+\.\d+)", (REPO / "CMakeLists.txt").read_text())
        if not project_version or version != project_version.group(1):
            raise RuntimeError("binary version differs from this checkout")
        architecture = run(["/usr/bin/lipo", "-archs", str(original)])
        if architecture != platform.machine() or architecture not in {"arm64", "x86_64"}:
            raise RuntimeError("package requires a binary built for this native Mac architecture")
        name = f"ava-{version}-macos-{architecture}"
        stage = work / name
        run([args.cmake, "--install", str(build), "--prefix", str(stage), "--component", "ava"])
        binary = stage / "bin/ava"
        binary.unlink()
        snapshot(original, binary)
        libraries = bundle_libraries(binary, stage)
        commands = run(["/usr/bin/otool", "-l", str(binary)])
        minos = re.search(r"\bminos ([0-9.]+)", commands)
        if not minos:
            raise RuntimeError("missing Mach-O minimum macOS version")
        documentation = stage / "share/doc/ava"
        artifact_readme = documentation / "README.md"
        text = artifact_readme.read_text()
        intro_end = text.index("## Basic commands")
        text = (f"# AVA macOS Artifact\n\nNative {architecture}; requires macOS {minos.group(1)} or newer. "
                "The archive includes its ncurses runtime libraries; Homebrew is not required to run it. "
                "Keep bin/, lib/, and share/ together when moving the extracted folder. "
                "Run bin/ava or add the extracted bin directory to PATH.\n\n"
                "This is an ad-hoc-signed local development artifact, not a notarized public release. "
                "PROVENANCE.json identifies the exact binary and bundled libraries. "
                "Native command containment is unavailable on macOS by design. Commands that require containment "
                "use one-time Critical approval; session grants and persistent Allow rules cannot bypass it. "
                "Executable identity/path checks, inherited-FD cleanup, the synthetic environment, and process-group cleanup remain active. "
                "The final pathname revalidation and exec are not atomic; a residual pathname-swap window remains. "
                "This is not Linux-equivalent isolation.\n\n" + text[intro_end:])
        artifact_readme.write_text(text)
        notices = documentation / "THIRD_PARTY_NOTICES.md"
        notices.write_text(notices.read_text().replace(
            "This notice applies to the AVA Linux binary distribution.",
            "This notice applies to the AVA binary distribution.").replace(
            "they are **not bundled** in the archive.",
            "the macOS archive bundles ncurses under lib/ with its full notice in licenses/ncurses-COPYING. "
            "Apple system libraries and frameworks are supplied by macOS."))
        provenance = {"schema_version": 1, "platform": "macos", "architecture": architecture,
                      "minimum_macos": minos.group(1), "version": version, "release_qualified": False,
                      "signing": "ad-hoc", "notarized": False,
                      "source_commit": run(["git", "-C", str(REPO), "rev-parse", "HEAD"]),
                      "source_dirty": bool(run(["git", "-C", str(REPO), "status", "--porcelain", "--untracked-files=normal"])),
                      "input_binary_sha256": digest(original), "binary_sha256": digest(binary), "libraries": libraries}
        (documentation / "PROVENANCE.json").write_text(json.dumps(provenance, indent=2) + "\n")
        archive = work / f"{name}.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            for path in sorted(stage.rglob("*")):
                if path.is_symlink() or not (path.is_dir() or path.is_file()):
                    raise RuntimeError(f"unexpected archive entry: {path.relative_to(stage)}")
            tar.add(stage, arcname=name)
        # Smoke the exact archived bytes after relocation, in an isolated HOME.
        relocated = work / "relocated"
        relocated.mkdir()
        extract_archive(archive, relocated)
        relocated_binary = relocated / name / "bin/ava"
        if run([str(relocated_binary), "--version"], env=environment, cwd=work) != version_output:
            raise RuntimeError("relocated archive version smoke failed")
        if "Usage" not in run([str(relocated_binary), "--help"], env=environment, cwd=work):
            raise RuntimeError("relocated archive help smoke failed")
        run([sys.executable, str(REPO / "scripts/verify-markdown-links.py"), str(relocated / name)])
        checksum = digest(archive)
        if args.output_dir:
            output.mkdir(mode=0o700)
        with (output / archive.name).open("xb") as target, archive.open("rb") as source:
            shutil.copyfileobj(source, target)
        with (output / f"{archive.name}.sha256").open("x") as target:
            target.write(f"{checksum}  {archive.name}\n")
        print(f"artifact: {output / archive.name}")
        print(f"sha256: {checksum}")
        print("relocated version/help and documentation checks: passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        detail = error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) and error.stderr else str(error)
        print(f"error: {detail}", file=sys.stderr)
        raise SystemExit(1) from error
