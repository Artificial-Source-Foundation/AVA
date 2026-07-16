#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import importlib.util
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
import time


def run(
    command: list[str],
    *,
    env: dict[str, str],
    check: bool = True,
    timeout: float = 120.0,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, timeout=timeout)
    if check and result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def start(command: list[str], *, env: dict[str, str]) -> subprocess.Popen[str]:
    return subprocess.Popen(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)


def finish(process: subprocess.Popen[str], *, timeout: float = 120.0) -> subprocess.CompletedProcess[str]:
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, stderr = process.communicate()
        raise RuntimeError(f"timed out waiting for package process\nstdout:\n{stdout}\nstderr:\n{stderr}")
    return subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)


def wait_for_path(path: pathlib.Path, process: subprocess.Popen[str], *, timeout: float = 15.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        if process.poll() is not None:
            result = finish(process)
            raise RuntimeError(
                f"package process exited before creating synchronization marker {path}\n"
                f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
            )
        time.sleep(0.01)
    process.kill()
    result = finish(process)
    raise RuntimeError(
        f"package process did not create synchronization marker {path}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )


def parse_path(output: str, label: str) -> pathlib.Path:
    prefix = f"{label}: "
    for line in output.splitlines():
        if line.startswith(prefix):
            return pathlib.Path(line[len(prefix) :])
    raise RuntimeError(f"package output did not report {label}:\n{output}")


def project_version(repo: pathlib.Path) -> str:
    text = (repo / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(
        r"project\s*\(\s*ava\b.*?\bVERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\b",
        text,
        re.DOTALL | re.IGNORECASE,
    )
    if not match:
        raise RuntimeError("unable to parse top-level project(ava VERSION X.Y.Z)")
    return match.group(1)


def package_architecture() -> str:
    machine = platform.machine()
    known = {
        "x86_64": "x64",
        "amd64": "x64",
        "aarch64": "arm64",
        "arm64": "arm64",
        "armv7l": "armv7",
        "armv7": "armv7",
        "ppc64le": "ppc64le",
        "riscv64": "riscv64",
    }
    if machine in known:
        return known[machine]
    normalized = re.sub(r"[^a-z0-9._-]", "-", machine.lower())
    if not normalized:
        raise RuntimeError("unable to normalize host architecture in package test")
    return normalized


def expected_files(package_name: str) -> set[str]:
    docs = {
        "README.md",
        "LICENSE",
        "docs/USAGE.md",
        "docs/CONFIG.md",
        "docs/TESTING.md",
        "docs/headless-protocol.md",
        "docs/rpc-protocol.md",
        "docs/acp.md",
        "docs/mcp.md",
        "docs/session-format.md",
        "docs/plugin-system.md",
        "docs/plugin-compatibility-policy.md",
        "docs/release-checklist.md",
        "docs/engineering/session-versioning.md",
        "docs/engineering/side-effect-safety-checklist.md",
        "docs/interop/evidence/README.md",
        "docs/interop/evidence/zed-1.9.0-2026-07-14.md",
        "docs/product/mvp-coverage-ledger.md",
        "docs/acp-support.json",
        "docs/schema/theme.schema.json",
    }
    return {f"{package_name}/bin/ava"} | {f"{package_name}/share/doc/ava/{doc}" for doc in docs}


def write_executable(path: pathlib.Path, contents: str) -> None:
    path.write_text(contents, encoding="utf-8")
    path.chmod(0o700)


def write_ava_fixture(path: pathlib.Path, version: str) -> None:
    write_executable(
        path,
        f"""#!/bin/sh
set -eu
case "${{1-}}" in
  --version)
    if [ -n "${{AVA_PACKAGE_TEST_MARKER:-}}" ] && [ ! -e "$AVA_PACKAGE_TEST_MARKER" ]; then
      : > "$AVA_PACKAGE_TEST_MARKER"
      while [ ! -e "$AVA_PACKAGE_TEST_GATE" ]; do sleep 0.01; done
    fi
    printf 'ava {version}\\n'
    ;;
  --help)
    printf 'Usage: ava [options]\\n'
    ;;
  packages)
    if [ "${{2-}}" != list ]; then exit 2; fi
    printf 'package management deferred\\n'
    ;;
  *)
    printf 'unexpected fixture arguments\\n' >&2
    exit 2
    ;;
esac
""",
    )


def write_mutating_ava_fixture(path: pathlib.Path, version: str) -> None:
    write_executable(
        path,
        f"""#!/bin/sh
set -eu
case "${{1-}}" in
  --version)
    if [ ! -e "$AVA_PACKAGE_TEST_BUILD_MUTATION_MARKER" ]; then
      : > "$AVA_PACKAGE_TEST_BUILD_MUTATION_MARKER"
      install -m 0700 "$AVA_PACKAGE_TEST_BUILD_AVA_REPLACEMENT" "$AVA_PACKAGE_TEST_BUILD_AVA_ORIGINAL.next"
      mv "$AVA_PACKAGE_TEST_BUILD_AVA_ORIGINAL.next" "$AVA_PACKAGE_TEST_BUILD_AVA_ORIGINAL"
      install -m 0700 "$AVA_PACKAGE_TEST_BUILD_FAKE_REPLACEMENT" "$AVA_PACKAGE_TEST_BUILD_FAKE_ORIGINAL.next"
      mv "$AVA_PACKAGE_TEST_BUILD_FAKE_ORIGINAL.next" "$AVA_PACKAGE_TEST_BUILD_FAKE_ORIGINAL"
    fi
    printf 'ava {version}\\n'
    ;;
  --help)
    printf 'Usage: ava [options]\\n'
    ;;
  packages)
    if [ "${{2-}}" != list ]; then exit 2; fi
    printf 'package management deferred\\n'
    ;;
  *)
    printf 'unexpected fixture arguments\\n' >&2
    exit 2
    ;;
esac
""",
    )


def package_command(
    script: pathlib.Path,
    binary: pathlib.Path,
    output: pathlib.Path | None = None,
    fake_provider: pathlib.Path | None = None,
) -> list[str]:
    command = [str(script), "--binary", str(binary)]
    if fake_provider is not None:
        command.extend(["--fake-provider", str(fake_provider)])
    if output is not None:
        command.extend(["--output-dir", str(output)])
    return command


def build_package_command(script: pathlib.Path, output: pathlib.Path) -> list[str]:
    return [str(script), "--output-dir", str(output)]


def load_publisher(path: pathlib.Path):
    spec = importlib.util.spec_from_file_location("ava_publish_linux_artifacts_test", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load publication helper from {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require_failure(result: subprocess.CompletedProcess[str], message: str, context: str) -> None:
    if result.returncode == 0 or message not in result.stderr:
        raise RuntimeError(
            f"{context}\nreturn code: {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def assert_output_empty(path: pathlib.Path, context: str) -> None:
    entries = list(path.iterdir())
    if entries:
        raise RuntimeError(f"{context}: {[entry.name for entry in entries]}")


def create_fake_build_repository(
    repo: pathlib.Path,
    script: pathlib.Path,
    publisher: pathlib.Path,
    root: pathlib.Path,
    package_name: str,
    version: str,
) -> tuple[pathlib.Path, pathlib.Path]:
    fake_repo = root / "build-mode-repository"
    (fake_repo / "scripts").mkdir(parents=True, mode=0o700)
    (fake_repo / "tests").mkdir(mode=0o700)
    shutil.copy2(script, fake_repo / "scripts" / "package-linux.sh")
    shutil.copy2(publisher, fake_repo / "scripts" / "publish-linux-artifacts.py")
    shutil.copy2(repo / "scripts" / "verify-markdown-links.py", fake_repo / "scripts" / "verify-markdown-links.py")
    (fake_repo / "CMakeLists.txt").write_text(
        f"cmake_minimum_required(VERSION 3.25)\nproject(ava VERSION {version})\n",
        encoding="utf-8",
    )

    install_manifest: list[tuple[str, str]] = []
    package_prefix = f"{package_name}/"
    for member in sorted(expected_files(package_name)):
        relative = member.removeprefix(package_prefix)
        if relative == "bin/ava":
            continue
        if relative == "share/doc/ava/README.md":
            source_relative = "docs/release-artifact-readme.md"
        elif relative == "share/doc/ava/LICENSE":
            source_relative = "LICENSE"
        else:
            doc_prefix = "share/doc/ava/"
            if not relative.startswith(doc_prefix):
                raise RuntimeError(f"unexpected package fixture member: {relative}")
            source_relative = relative.removeprefix(doc_prefix)
        source = repo / source_relative
        destination = fake_repo / source_relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        install_manifest.append((source_relative, relative))

    (fake_repo / "install-manifest.txt").write_text(
        "".join(f"{source}\t{destination}\n" for source, destination in install_manifest),
        encoding="utf-8",
    )
    (fake_repo / "tests" / "cli_headless_e2e_model_smoke.cmake").write_text(
        "# The fake cmake wrapper validates deterministic model-smoke inputs.\n",
        encoding="utf-8",
    )

    wrapper_dir = root / "build-mode-cmake-wrapper"
    wrapper_dir.mkdir(mode=0o700)
    fake_cmake = wrapper_dir / "cmake"
    write_executable(
        fake_cmake,
        """#!/usr/bin/env python3
import hashlib
import os
import pathlib
import shutil
import sys

args = sys.argv[1:]
repo = pathlib.Path(os.environ["AVA_PACKAGE_TEST_BUILD_REPO"])

if args and args[0] == "--preset":
    raise SystemExit(0)

if args and args[0] == "--build":
    ava = repo / "build-release" / "ava"
    fake = repo / "build-release" / "tests" / "ava_fake_provider_server"
    ava.parent.mkdir(parents=True, exist_ok=True)
    fake.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(os.environ["AVA_PACKAGE_TEST_BUILD_AVA_TEMPLATE"], ava)
    shutil.copy2(os.environ["AVA_PACKAGE_TEST_BUILD_FAKE_TEMPLATE"], fake)
    ava.chmod(0o700)
    fake.chmod(0o700)
    raise SystemExit(0)

if args and args[0] == "--install":
    try:
        prefix = pathlib.Path(args[args.index("--prefix") + 1])
    except (ValueError, IndexError) as exc:
        raise RuntimeError("fake cmake install did not receive --prefix") from exc
    installed_ava = prefix / "bin" / "ava"
    installed_ava.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(repo / "build-release" / "ava", installed_ava)
    installed_ava.chmod(0o755)
    for line in (repo / "install-manifest.txt").read_text(encoding="utf-8").splitlines():
        source_name, destination_name = line.split("\\t", 1)
        destination = prefix / destination_name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(repo / source_name, destination)
        destination.chmod(0o644)
    raise SystemExit(0)


def definition(prefix):
    values = [argument[len(prefix):] for argument in args if argument.startswith(prefix)]
    if len(values) != 1:
        raise RuntimeError(f"expected exactly one {prefix} definition")
    return pathlib.Path(values[0])


ava = definition("-DAVA_EXE=")
fake = definition("-DAVA_FAKE_PROVIDER_EXE=")
if fake == repo / "build-release" / "tests" / "ava_fake_provider_server":
    raise RuntimeError("model smoke received mutable build-tree fake provider")
if fake.name != "fake-provider" or fake.parent.name != "inputs":
    raise RuntimeError(f"model smoke did not receive the private fake-provider snapshot: {fake}")
ava_digest = hashlib.sha256(ava.read_bytes()).hexdigest()
fake_digest = hashlib.sha256(fake.read_bytes()).hexdigest()
if ava_digest != os.environ["AVA_PACKAGE_TEST_BUILD_AVA_DIGEST"]:
    raise RuntimeError(f"model-smoke AVA digest changed: {ava_digest}")
if fake_digest != os.environ["AVA_PACKAGE_TEST_BUILD_FAKE_DIGEST"]:
    raise RuntimeError(f"model-smoke fake-provider digest changed: {fake_digest}")
if hashlib.sha256((repo / "build-release" / "ava").read_bytes()).hexdigest() == ava_digest:
    raise RuntimeError("build-tree AVA was not replaced before install/model smoke")
if hashlib.sha256((repo / "build-release" / "tests" / "ava_fake_provider_server").read_bytes()).hexdigest() == fake_digest:
    raise RuntimeError("build-tree fake provider was not replaced before model smoke")
pathlib.Path(os.environ["AVA_PACKAGE_TEST_BUILD_MODEL_MARKER"]).write_text("smoked\\n", encoding="utf-8")
""",
    )
    return fake_repo, wrapper_dir


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", type=pathlib.Path, required=True)
    parser.add_argument("--ava", type=pathlib.Path, required=True)
    parser.add_argument("--fake-provider", type=pathlib.Path, required=True)
    parser.add_argument("--repo", type=pathlib.Path, required=True)
    parser.add_argument("--root", type=pathlib.Path, required=True)
    args = parser.parse_args()

    if platform.system() != "Linux":
        print("skipping Linux package tests on non-Linux host")
        return 77

    requested_root = args.root.resolve()
    repo = args.repo.resolve()
    script = args.script.resolve()
    publisher = script.parent / "publish-linux-artifacts.py"
    version = project_version(repo)
    package_name = f"ava-{version}-linux-{package_architecture()}"
    archive_name = f"{package_name}.tar.gz"
    checksum_name = f"{archive_name}.sha256"

    temporary_root = requested_root == repo or repo in requested_root.parents
    if temporary_root:
        root = pathlib.Path(tempfile.mkdtemp(prefix="ava-package-linux-tests-"))
    else:
        root = requested_root
        shutil.rmtree(root, ignore_errors=True)
        root.mkdir(parents=True, mode=0o700)
    temp = root / "tmp"
    temp.mkdir(mode=0o700)
    env = os.environ.copy()
    env["TMPDIR"] = str(temp)

    # Exercise the real built CLI and fake provider once for the complete model smoke.
    output = root / "accepted-output"
    output.mkdir(mode=0o700)
    success = run(
        package_command(script, args.ava.resolve(), output, args.fake_provider.resolve()),
        env=env,
    )
    artifact = parse_path(success.stdout, "artifact")
    checksum = parse_path(success.stdout, "checksum")
    if artifact != output / archive_name or checksum != output / checksum_name:
        raise RuntimeError(f"package did not use deterministic artifact names: {artifact}, {checksum}")
    if not artifact.is_file() or not checksum.is_file():
        raise RuntimeError("accepted-binary package did not publish the expected archive pair")
    digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
    checksum_parts = checksum.read_text(encoding="utf-8").split()
    if checksum_parts != [digest, artifact.name]:
        raise RuntimeError(f"checksum file does not describe the adjacent archive: {checksum_parts}")
    with tarfile.open(artifact, "r:gz") as archive:
        members = archive.getmembers()
        regular_files = {member.name for member in members if member.isfile()}
        if any(member.issym() or member.islnk() for member in members):
            raise RuntimeError("package archive contains a link")
        if regular_files != expected_files(package_name):
            raise RuntimeError(
                f"archive allowlist mismatch\nactual={sorted(regular_files)}\n"
                f"expected={sorted(expected_files(package_name))}"
            )
        extract = root / "independent-extract"
        extract.mkdir(mode=0o700)
        archive.extractall(extract, filter="data")
    extracted_ava = extract / package_name / "bin" / "ava"
    extracted_version = run([str(extracted_ava), "--version"], env=env).stdout.strip()
    if extracted_version != f"ava {version}":
        raise RuntimeError(f"independently extracted CLI smoke returned unexpected version: {extracted_version}")
    if "Usage" not in run([str(extracted_ava), "--help"], env=env).stdout:
        raise RuntimeError("independently extracted CLI help smoke failed")

    fixture = root / "fixture-ava"
    write_ava_fixture(fixture, version)

    default_result = run(package_command(script, fixture), env=env)
    default_artifact = parse_path(default_result.stdout, "artifact")
    default_output = default_artifact.parent
    if default_output == output or not default_output.name.startswith("ava-release-output."):
        raise RuntimeError(f"default output was not unpredictable and distinct: {default_output}")
    if stat.S_IMODE(default_output.stat().st_mode) != 0o700:
        raise RuntimeError(f"default output is not private mode 0700: {oct(stat.S_IMODE(default_output.stat().st_mode))}")
    shutil.rmtree(default_output)

    insecure = root / "insecure-output"
    insecure.mkdir(mode=0o700)
    insecure.chmod(0o755)
    insecure_result = run(package_command(script, fixture, insecure), env=env, check=False)
    require_failure(insecure_result, "exact mode 0700", "non-0700 output was not rejected")

    in_repo = repo / "build" / "package-linux-in-repo-negative"
    in_repo_result = run(package_command(script, fixture, in_repo), env=env, check=False)
    require_failure(in_repo_result, "outside the repository", "in-repository output was not rejected")

    symlink_directory_target = root / "symlink-directory-target"
    symlink_directory_target.mkdir(mode=0o700)
    symlink_directory = root / "symlink-directory"
    symlink_directory.symlink_to(symlink_directory_target, target_is_directory=True)
    symlink_directory_result = run(package_command(script, fixture, symlink_directory), env=env, check=False)
    require_failure(symlink_directory_result, "must not be a symlink", "symlink output directory was not rejected")

    mismatch = root / "ava-mismatched-version"
    write_ava_fixture(mismatch, "9.9.9")
    mismatch_output = root / "mismatch-output"
    mismatch_output.mkdir(mode=0o700)
    mismatch_result = run(package_command(script, mismatch, mismatch_output), env=env, check=False)
    require_failure(mismatch_result, "does not match current checkout", "mismatched accepted binary version was not rejected")

    binary_symlink = root / "binary-symlink"
    binary_symlink.symlink_to(fixture)
    binary_symlink_output = root / "binary-symlink-output"
    binary_symlink_output.mkdir(mode=0o700)
    binary_symlink_result = run(package_command(script, binary_symlink, binary_symlink_output), env=env, check=False)
    require_failure(
        binary_symlink_result,
        "must not be a symlink",
        "accepted --binary final symlink was not rejected",
    )

    non_executable = root / "non-executable-binary"
    non_executable.write_bytes(fixture.read_bytes())
    non_executable.chmod(0o600)
    non_executable_output = root / "non-executable-output"
    non_executable_output.mkdir(mode=0o700)
    non_executable_result = run(
        package_command(script, non_executable, non_executable_output), env=env, check=False
    )
    require_failure(
        non_executable_result,
        "must name an executable regular file",
        "non-executable accepted --binary was not rejected",
    )

    # Mutating an already-open source inode during its copy must invalidate and
    # remove the private snapshot, even when the source pathname never changes.
    publisher_module = load_publisher(publisher)
    changing_source = root / "changing-snapshot-source"
    write_executable(changing_source, "#!/bin/sh\nexit 0\n")
    changing_destination_root = root / "changing-snapshot-destination"
    changing_destination_root.mkdir(mode=0o700)
    changing_destination = changing_destination_root / "ava"
    original_descriptor_copy = publisher_module.copy_file_descriptors

    def copy_then_mutate(source_fd: int, target_fd: int, source_path: pathlib.Path) -> None:
        original_descriptor_copy(source_fd, target_fd, source_path)
        mutation_fd = os.open(source_path, os.O_WRONLY | os.O_APPEND)
        try:
            os.write(mutation_fd, b"# changed in place\n")
            os.fsync(mutation_fd)
        finally:
            os.close(mutation_fd)

    publisher_module.copy_file_descriptors = copy_then_mutate
    try:
        try:
            publisher_module.snapshot_executable(changing_source, changing_destination)
        except RuntimeError as exc:
            if "changed while copying" not in str(exc):
                raise RuntimeError(f"source-mutation snapshot failed for the wrong reason: {exc}") from exc
        else:
            raise RuntimeError("in-place source mutation did not invalidate the executable snapshot")
    finally:
        publisher_module.copy_file_descriptors = original_descriptor_copy
    if changing_destination.exists():
        raise RuntimeError("invalid executable snapshot remained after in-place source mutation")

    fake_target = root / "fake-provider-fixture"
    write_executable(fake_target, "#!/bin/sh\nexit 0\n")
    fake_symlink = root / "fake-provider-symlink"
    fake_symlink.symlink_to(fake_target)
    fake_symlink_output = root / "fake-symlink-output"
    fake_symlink_output.mkdir(mode=0o700)
    fake_symlink_result = run(
        package_command(script, fixture, fake_symlink_output, fake_symlink), env=env, check=False
    )
    require_failure(
        fake_symlink_result,
        "must not be a symlink",
        "accepted --fake-provider final symlink was not rejected",
    )

    # Block the snapshotted CLI after both accepted inputs have been copied, replace
    # both original pathnames, and prove packaging/model smoke still use old bytes.
    snapshot_ava = root / "snapshot-ava"
    write_ava_fixture(snapshot_ava, version)
    accepted_ava_bytes = snapshot_ava.read_bytes()
    snapshot_fake = root / "snapshot-fake-provider"
    write_executable(snapshot_fake, "#!/bin/sh\nprintf 'accepted fake provider\\n'\n")
    accepted_fake_digest = hashlib.sha256(snapshot_fake.read_bytes()).hexdigest()
    snapshot_marker = root / "snapshot-version-started"
    snapshot_gate = root / "snapshot-version-release"
    fake_smoke_marker = root / "snapshot-fake-smoked"
    wrapper_dir = root / "cmake-wrapper"
    wrapper_dir.mkdir(mode=0o700)
    write_executable(
        wrapper_dir / "cmake",
        """#!/usr/bin/env python3
import hashlib
import os
import pathlib
import sys

prefix = "-DAVA_FAKE_PROVIDER_EXE="
values = [argument[len(prefix):] for argument in sys.argv[1:] if argument.startswith(prefix)]
if len(values) != 1:
    print("expected exactly one fake-provider CMake definition", file=sys.stderr)
    raise SystemExit(2)
snapshot = pathlib.Path(values[0])
original = pathlib.Path(os.environ["AVA_PACKAGE_TEST_FAKE_ORIGINAL"])
if snapshot == original or snapshot.name != "fake-provider" or snapshot.parent.name != "inputs":
    print(f"fake provider was not a private input snapshot: {snapshot}", file=sys.stderr)
    raise SystemExit(3)
actual = hashlib.sha256(snapshot.read_bytes()).hexdigest()
if actual != os.environ["AVA_PACKAGE_TEST_FAKE_DIGEST"]:
    print(f"fake-provider snapshot digest changed: {actual}", file=sys.stderr)
    raise SystemExit(4)
pathlib.Path(os.environ["AVA_PACKAGE_TEST_FAKE_SMOKE_MARKER"]).write_text("smoked\\n", encoding="utf-8")
""",
    )
    snapshot_output = root / "snapshot-output"
    snapshot_output.mkdir(mode=0o700)
    snapshot_env = env.copy()
    snapshot_env["PATH"] = f"{wrapper_dir}{os.pathsep}{env['PATH']}"
    snapshot_env["AVA_PACKAGE_TEST_MARKER"] = str(snapshot_marker)
    snapshot_env["AVA_PACKAGE_TEST_GATE"] = str(snapshot_gate)
    snapshot_env["AVA_PACKAGE_TEST_FAKE_ORIGINAL"] = str(snapshot_fake)
    snapshot_env["AVA_PACKAGE_TEST_FAKE_DIGEST"] = accepted_fake_digest
    snapshot_env["AVA_PACKAGE_TEST_FAKE_SMOKE_MARKER"] = str(fake_smoke_marker)
    snapshot_process = start(
        package_command(script, snapshot_ava, snapshot_output, snapshot_fake),
        env=snapshot_env,
    )
    wait_for_path(snapshot_marker, snapshot_process)
    replacement_ava = root / "snapshot-ava-replacement"
    write_ava_fixture(replacement_ava, "9.9.9")
    os.replace(replacement_ava, snapshot_ava)
    replacement_fake = root / "snapshot-fake-replacement"
    write_executable(replacement_fake, "#!/bin/sh\nprintf 'replacement fake provider\\n'\n")
    os.replace(replacement_fake, snapshot_fake)
    snapshot_gate.touch()
    snapshot_result = finish(snapshot_process)
    if snapshot_result.returncode != 0:
        raise RuntimeError(
            f"snapshot-pinning package failed\nstdout:\n{snapshot_result.stdout}\nstderr:\n{snapshot_result.stderr}"
        )
    if not fake_smoke_marker.is_file():
        raise RuntimeError("snapshot-pinning test did not exercise the snapshotted fake provider")
    snapshot_artifact = parse_path(snapshot_result.stdout, "artifact")
    with tarfile.open(snapshot_artifact, "r:gz") as archive:
        packaged_ava = archive.extractfile(f"{package_name}/bin/ava")
        if packaged_ava is None or packaged_ava.read() != accepted_ava_bytes:
            raise RuntimeError("archive did not contain the one-time accepted --binary snapshot")
    if snapshot_ava.read_bytes() == accepted_ava_bytes:
        raise RuntimeError("snapshot race did not replace the original --binary pathname")
    if hashlib.sha256(snapshot_fake.read_bytes()).hexdigest() == accepted_fake_digest:
        raise RuntimeError("snapshot race did not replace the original --fake-provider pathname")

    # Exercise build mode without recursively building AVA. Fake cmake creates
    # both build outputs; the snapshotted AVA replaces those mutable paths on
    # its first --version call. Fake install deliberately stages the replaced
    # AVA, so only an unconditional overwrite from the snapshot can pass.
    build_ava_template = root / "build-mode-ava-template"
    write_mutating_ava_fixture(build_ava_template, version)
    build_ava_bytes = build_ava_template.read_bytes()
    build_ava_digest = hashlib.sha256(build_ava_bytes).hexdigest()
    build_fake_template = root / "build-mode-fake-template"
    write_executable(build_fake_template, "#!/bin/sh\nprintf 'snapshotted build fake provider\\n'\n")
    build_fake_digest = hashlib.sha256(build_fake_template.read_bytes()).hexdigest()
    build_ava_replacement = root / "build-mode-ava-replacement"
    write_ava_fixture(build_ava_replacement, "9.9.9")
    build_fake_replacement = root / "build-mode-fake-replacement"
    write_executable(build_fake_replacement, "#!/bin/sh\nprintf 'mutable replacement fake provider\\n'\n")
    fake_build_repo, build_wrapper_dir = create_fake_build_repository(
        repo,
        script,
        publisher,
        root,
        package_name,
        version,
    )
    build_mutation_marker = root / "build-mode-mutated"
    build_model_marker = root / "build-mode-model-smoked"
    build_output = root / "build-mode-output"
    build_output.mkdir(mode=0o700)
    build_env = env.copy()
    build_env["PATH"] = f"{build_wrapper_dir}{os.pathsep}{env['PATH']}"
    build_env["AVA_PACKAGE_TEST_BUILD_REPO"] = str(fake_build_repo)
    build_env["AVA_PACKAGE_TEST_BUILD_AVA_TEMPLATE"] = str(build_ava_template)
    build_env["AVA_PACKAGE_TEST_BUILD_FAKE_TEMPLATE"] = str(build_fake_template)
    build_env["AVA_PACKAGE_TEST_BUILD_AVA_ORIGINAL"] = str(fake_build_repo / "build-release" / "ava")
    build_env["AVA_PACKAGE_TEST_BUILD_FAKE_ORIGINAL"] = str(
        fake_build_repo / "build-release" / "tests" / "ava_fake_provider_server"
    )
    build_env["AVA_PACKAGE_TEST_BUILD_AVA_REPLACEMENT"] = str(build_ava_replacement)
    build_env["AVA_PACKAGE_TEST_BUILD_FAKE_REPLACEMENT"] = str(build_fake_replacement)
    build_env["AVA_PACKAGE_TEST_BUILD_MUTATION_MARKER"] = str(build_mutation_marker)
    build_env["AVA_PACKAGE_TEST_BUILD_MODEL_MARKER"] = str(build_model_marker)
    build_env["AVA_PACKAGE_TEST_BUILD_AVA_DIGEST"] = build_ava_digest
    build_env["AVA_PACKAGE_TEST_BUILD_FAKE_DIGEST"] = build_fake_digest
    build_result = run(
        build_package_command(fake_build_repo / "scripts" / "package-linux.sh", build_output),
        env=build_env,
    )
    if not build_mutation_marker.is_file() or not build_model_marker.is_file():
        raise RuntimeError("fake build-mode harness did not exercise mutation and deterministic model smoke")
    if hashlib.sha256((fake_build_repo / "build-release" / "ava").read_bytes()).hexdigest() == build_ava_digest:
        raise RuntimeError("fake build-mode AVA pathname was not replaced after snapshot")
    if (
        hashlib.sha256(
            (fake_build_repo / "build-release" / "tests" / "ava_fake_provider_server").read_bytes()
        ).hexdigest()
        == build_fake_digest
    ):
        raise RuntimeError("fake build-mode provider pathname was not replaced after snapshot")
    build_artifact = parse_path(build_result.stdout, "artifact")
    with tarfile.open(build_artifact, "r:gz") as archive:
        packaged_build_ava = archive.extractfile(f"{package_name}/bin/ava")
        if packaged_build_ava is None or packaged_build_ava.read() != build_ava_bytes:
            raise RuntimeError("build-mode archive did not contain the exact private AVA snapshot")

    # Replace the approved output pathname while packaging is blocked. A fresh,
    # owner-owned 0700 directory at the same path must not inherit approval.
    identity_ava = root / "identity-ava"
    write_ava_fixture(identity_ava, version)
    identity_marker = root / "identity-version-started"
    identity_gate = root / "identity-version-release"
    identity_output = root / "identity-output"
    identity_output.mkdir(mode=0o700)
    identity_env = env.copy()
    identity_env["AVA_PACKAGE_TEST_MARKER"] = str(identity_marker)
    identity_env["AVA_PACKAGE_TEST_GATE"] = str(identity_gate)
    identity_process = start(package_command(script, identity_ava, identity_output), env=identity_env)
    wait_for_path(identity_marker, identity_process)
    detached_approved_output = root / "identity-output-approved-original"
    identity_output.rename(detached_approved_output)
    identity_output.mkdir(mode=0o700)
    identity_gate.touch()
    identity_result = finish(identity_process)
    require_failure(
        identity_result,
        "identity changed since approval",
        "replacement output directory incorrectly inherited initial approval",
    )
    assert_output_empty(identity_output, "replacement output directory received publication files")
    assert_output_empty(detached_approved_output, "detached approved output directory received publication files")

    first_source = root / "pair-first"
    second_source = root / "pair-second"
    first_source.write_bytes(b"first\n")
    second_source.write_bytes(b"second\n")

    # Deliver SIGTERM immediately after publishing the checksum (the first
    # final rename). The helper must restore handlers and roll back that final
    # plus the archive temporary, leaving no half-pair or private temporary.
    cancellation_output = root / "cancellation-rollback-output"
    cancellation_output.mkdir(mode=0o700)
    cancellation_identity = run(
        [sys.executable, str(publisher), "--check", str(cancellation_output)], env=env
    ).stdout.strip()
    cancellation_driver = root / "interrupt-publication.py"
    cancellation_driver.write_text(
        """import importlib.util
import os
import pathlib
import signal
import sys

publisher_path, output_path, identity, checksum_source, archive_source = sys.argv[1:]
spec = importlib.util.spec_from_file_location("ava_publish_interruption_test", publisher_path)
if spec is None or spec.loader is None:
    raise SystemExit("could not load publisher")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
original_rename = module.rename_no_replace
rename_count = [0]


def rename_then_interrupt(directory_fd, temporary_name, final_name):
    original_rename(directory_fd, temporary_name, final_name)
    rename_count[0] += 1
    if rename_count[0] == 1:
        if not final_name.endswith(".sha256"):
            raise RuntimeError(f"archive was published before checksum: {final_name}")
        os.kill(os.getpid(), signal.SIGTERM)


module.rename_no_replace = rename_then_interrupt
initial_sigint = signal.getsignal(signal.SIGINT)
initial_sigterm = signal.getsignal(signal.SIGTERM)
try:
    module.publish(
        pathlib.Path(output_path),
        [
            (pathlib.Path(checksum_source), "pair.tar.gz.sha256"),
            (pathlib.Path(archive_source), "pair.tar.gz"),
        ],
        module.parse_identity(identity),
    )
except module.PublicationCancelled as exc:
    if exc.signal_number != signal.SIGTERM:
        raise RuntimeError(f"unexpected cancellation signal: {exc.signal_number}") from exc
else:
    raise RuntimeError("SIGTERM after first final rename did not cancel publication")
if signal.getsignal(signal.SIGINT) != initial_sigint or signal.getsignal(signal.SIGTERM) != initial_sigterm:
    raise RuntimeError("publication did not restore Python signal handlers")
raise SystemExit(91)
""",
        encoding="utf-8",
    )
    cancellation_result = run(
        [
            sys.executable,
            str(cancellation_driver),
            str(publisher),
            str(cancellation_output),
            cancellation_identity,
            str(first_source),
            str(second_source),
        ],
        env=env,
        check=False,
    )
    if cancellation_result.returncode != 91:
        raise RuntimeError(
            "publication interruption regression did not reach the expected cancellation path\n"
            f"return code: {cancellation_result.returncode}\n"
            f"stdout:\n{cancellation_result.stdout}\nstderr:\n{cancellation_result.stderr}"
        )
    assert_output_empty(cancellation_output, "cancelled publication left a final or temporary")

    # Keep the ordinary second-rename failure regression: a duplicate second
    # final must roll back the first final and all private temporaries.
    pair_output = root / "pair-rollback-output"
    pair_output.mkdir(mode=0o700)
    approved_identity = run([sys.executable, str(publisher), "--check", str(pair_output)], env=env).stdout.strip()
    pair_result = run(
        [
            sys.executable,
            str(publisher),
            "--output",
            str(pair_output),
            "--expected-directory-identity",
            approved_identity,
            "--file",
            str(first_source),
            "duplicate.bin",
            "--file",
            str(second_source),
            "duplicate.bin",
        ],
        env=env,
        check=False,
    )
    require_failure(pair_result, "refusing to overwrite", "duplicate second publication did not fail")
    assert_output_empty(pair_output, "transactional pair rollback left an orphan or temporary")

    # Existing regular files and symlinks are never removed or overwritten.
    existing_output = root / "existing-output"
    existing_output.mkdir(mode=0o700)
    existing_identity = run([sys.executable, str(publisher), "--check", str(existing_output)], env=env).stdout.strip()
    existing = existing_output / "existing.bin"
    existing.write_bytes(b"preexisting\n")
    existing_result = run(
        [
            sys.executable,
            str(publisher),
            "--output",
            str(existing_output),
            "--expected-directory-identity",
            existing_identity,
            "--file",
            str(first_source),
            existing.name,
        ],
        env=env,
        check=False,
    )
    require_failure(existing_result, "must be empty", "nonempty publication directory was not rejected")
    if existing.read_bytes() != b"preexisting\n":
        raise RuntimeError("publication modified or removed an existing regular destination")

    symlink_output = root / "symlink-output"
    symlink_output.mkdir(mode=0o700)
    sentinel = root / "sentinel"
    sentinel.write_text("do not modify\n", encoding="utf-8")
    (symlink_output / archive_name).symlink_to(sentinel)
    symlink_result = run(package_command(script, fixture, symlink_output), env=env, check=False)
    require_failure(symlink_result, "must be empty", "nonempty output with a symlink was not rejected")
    if sentinel.read_text(encoding="utf-8") != "do not modify\n":
        raise RuntimeError("publication followed and modified an existing symlink target")
    if not (symlink_output / archive_name).is_symlink():
        raise RuntimeError("publication removed an existing destination symlink")

    print("Linux package snapshot, identity, rollback, allowlist, checksum, and smoke tests passed")
    if temporary_root:
        shutil.rmtree(root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
