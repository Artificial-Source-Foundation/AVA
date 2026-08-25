#!/usr/bin/env python3
"""Generate deterministic, privacy-safe release provenance for one AVA binary."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys
from typing import Any

SCHEMA_VERSION = 2
DIRECT_DEPENDENCIES = (
    ("cwds", "cwds", "LICENSE", "MIT"),
    ("aicxx", "cmake/aicxx", "LICENSE", "MIT"),
    ("utils", "utils", "LICENSE", "MIT"),
    ("threadsafe", "threadsafe", "LICENSE", "MIT"),
    ("enchantum", "enchantum", "LICENSE", "MIT"),
    ("nlohmann_json", "src/json", "LICENSE.MIT", "MIT"),
)
DEPENDENCY_USAGE = {
    "cwds": "bundled-source",
    "aicxx": "build-tool",
    "utils": "bundled-source",
    "threadsafe": "bundled-source",
    "enchantum": "bundled-source",
    "nlohmann_json": "bundled-source",
}
# This committed policy makes the direct-license evidence independently
# auditable. A license identifier alone cannot establish what file was used.
EXPECTED_GITLINK_REVISIONS = {
    "cwds": "1fb7c4edc7018d3354323e2fe8c98800281546da",
    "aicxx": "411eae316e75f798611afc5223d861b213e9d503",
    "utils": "5ed11a1763eb982efcbc4d8407433010a8a317be",
    "threadsafe": "76c3ccab0ef913f6c472175eb3994b20b5b40a0e",
    "enchantum": "0d6115a9eb3e6510e38c73566cd9bc0131ebfc8c",
    "nlohmann_json": "722c03495f9978eb727f480b6ea0742f652e06a9",
}
EXPECTED_LICENSE_SHA256 = {
    "cwds": "2a0ddf2fbcd1b778d2f44ebb6c8f47d8a508573e3365f4d031956ddffb8cc327",
    "aicxx": "dbe888a4dac5018ae7a4beb1ecfacd89de8d7abc7193024b95b1a0d2d6a45fe8",
    "utils": "57852af0de7f40e804b4d29a9ef76e4bb7a103c354577ee52ea59373292dd1fb",
    "threadsafe": "4ca3adc43b4bbd0419209d9b0b9459b0c0718422cf5c95909ea4fd7f2dc9ae61",
    "enchantum": "2eae4991d657b439acae6cab3ae1bbf5333085142cc44068a7ce331ebdb5b123",
    "nlohmann_json": "d64d1b4d948aa7751cfb613c02dc246aab7b358d760053e6580f01affe946906",
}
HOST_DYNAMIC_ALLOWLIST = frozenset({"libncursesw.so.6", "libtinfo.so.6", "libstdc++.so.6", "libm.so.6", "libgcc_s.so.1", "libc.so.6"})
ARCHITECTURE_DYNAMIC_ALLOWLISTS = {
    "x86_64": HOST_DYNAMIC_ALLOWLIST,
    "aarch64": HOST_DYNAMIC_ALLOWLIST | {"ld-linux-aarch64.so.1"},
}
QUALIFIED_ARCHITECTURES = frozenset({"x86_64", "aarch64"})
MACHINE_ARCHITECTURES = {3: "x86", 62: "x86_64", 183: "aarch64"}


def run_git(repo: pathlib.Path, *args: str) -> str | None:
    try:
        return subprocess.check_output(["git", "-C", str(repo), *args], text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def project_version(repo: pathlib.Path) -> str | None:
    try:
        text = (repo / "CMakeLists.txt").read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r"project\s*\(\s*ava\b.*?\bVERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\b", text, re.DOTALL | re.IGNORECASE)
    return match.group(1) if match else None


def gitlink_revision(repo: pathlib.Path, dependency_path: str) -> str | None:
    entry = run_git(repo, "ls-tree", "HEAD", "--", dependency_path)
    if not entry:
        return None
    fields = entry.split()
    return fields[2] if len(fields) >= 3 and fields[0] == "160000" else None


def worktree_dirty(repo: pathlib.Path) -> bool | None:
    # Full porcelain state includes untracked files, which may change a source
    # build through CMake or compiler inputs.
    status = run_git(repo, "status", "--porcelain", "--untracked-files=all")
    return None if status is None else bool(status)


def elf_metadata(path: pathlib.Path) -> tuple[str | None, list[str] | None]:
    """Return ELF architecture and loader-visible DT_NEEDED names."""
    try:
        data = path.read_bytes()
    except OSError:
        return None, None
    if len(data) < 20 or data[:4] != b"\x7fELF" or data[5] not in (1, 2) or data[6] != 1:
        return None, None

    endian = "<" if data[5] == 1 else ">"
    elf_class = data[4]
    if elf_class == 1:
        header_size = 52
        phoff_offset = 28
        phentsize_offset = 42
        phnum_offset = 44
        program_format = endian + "IIIIIIII"
        dynamic_format = endian + "iI"
        max_word = (1 << 32) - 1
    elif elf_class == 2:
        header_size = 64
        phoff_offset = 32
        phentsize_offset = 54
        phnum_offset = 56
        program_format = endian + "IIQQQQQQ"
        dynamic_format = endian + "qQ"
        max_word = (1 << 64) - 1
    else:
        return None, None
    if len(data) < header_size:
        return None, None

    machine = struct.unpack_from(endian + "H", data, 18)[0]
    architecture = MACHINE_ARCHITECTURES.get(machine, f"elf-machine-{machine}")
    phoff = struct.unpack_from(endian + ("I" if elf_class == 1 else "Q"), data, phoff_offset)[0]
    phentsize, phnum = struct.unpack_from(endian + "HH", data, phentsize_offset)
    program_size = struct.calcsize(program_format)
    # PN_XNUM requires section-header interpretation, which is deliberately not
    # part of this loader-facing parser.
    if phoff < header_size or phnum == 0 or phnum == 0xFFFF or phentsize != program_size:
        return architecture, None
    if phoff > len(data) or phnum > (len(data) - phoff) // phentsize:
        return architecture, None

    dynamic_segments: list[tuple[int, int, int]] = []
    load_segments: list[tuple[int, int, int]] = []
    for index in range(phnum):
        fields = struct.unpack_from(program_format, data, phoff + index * phentsize)
        if elf_class == 1:
            segment_type, offset, virtual_address, _physical_address, file_size, memory_size, _flags, _alignment = fields
        else:
            segment_type, _flags, offset, virtual_address, _physical_address, file_size, memory_size, _alignment = fields
        if segment_type == 2:
            dynamic_segments.append((offset, virtual_address, file_size))
        elif segment_type == 1:
            # A PT_LOAD used for translation must identify an actual,
            # file-backed virtual-address range.
            if file_size == 0:
                continue
            if (file_size > memory_size or offset > len(data) or
                    file_size > len(data) - offset or virtual_address > max_word - file_size):
                return architecture, None
            load_segments.append((offset, virtual_address, file_size))

    if len(dynamic_segments) != 1 or not load_segments:
        return architecture, None
    dynamic_offset, dynamic_address, dynamic_size = dynamic_segments[0]
    dynamic_entry_size = struct.calcsize(dynamic_format)
    if (dynamic_size == 0 or dynamic_size % dynamic_entry_size != 0 or
            dynamic_offset > len(data) or dynamic_size > len(data) - dynamic_offset):
        return architecture, None

    def map_file_offsets(virtual_address: int, size: int) -> list[int]:
        mappings: list[int] = []
        for load_offset, load_address, load_size in load_segments:
            if virtual_address < load_address:
                continue
            address_delta = virtual_address - load_address
            if address_delta <= load_size and size <= load_size - address_delta:
                mappings.append(load_offset + address_delta)
        return mappings

    # PT_DYNAMIC is itself loader-visible only at its virtual address; require
    # its bytes to agree with one file-backed PT_LOAD translation.
    dynamic_mappings = map_file_offsets(dynamic_address, dynamic_size)
    if len(dynamic_mappings) != 1 or dynamic_mappings[0] != dynamic_offset:
        return architecture, None

    needed_offsets: list[int] = []
    string_table_address: int | None = None
    string_table_size: int | None = None
    terminated = False
    for entry_offset in range(dynamic_offset, dynamic_offset + dynamic_size, dynamic_entry_size):
        tag, value = struct.unpack_from(dynamic_format, data, entry_offset)
        if tag == 0:
            terminated = True
            break
        if tag == 1:
            needed_offsets.append(value)
        elif tag == 5:
            if string_table_address is not None:
                return architecture, None
            string_table_address = value
        elif tag == 10:
            if string_table_size is not None:
                return architecture, None
            string_table_size = value
    if not terminated or string_table_address is None or string_table_size is None or string_table_size == 0:
        return architecture, None
    if string_table_address > max_word or string_table_size > max_word - string_table_address:
        return architecture, None

    string_mappings = map_file_offsets(string_table_address, string_table_size)
    if len(string_mappings) != 1:
        return architecture, None
    string_offset = string_mappings[0]

    needed: list[str] = []
    for value in needed_offsets:
        if value >= string_table_size:
            return architecture, None
        name_start = string_offset + value
        name_end = data.find(b"\0", name_start, string_offset + string_table_size)
        if name_end == -1:
            return architecture, None
        try:
            needed.append(data[name_start:name_end].decode("utf-8"))
        except UnicodeDecodeError:
            return architecture, None
    return architecture, sorted(set(needed))


def dependency_records(repo: pathlib.Path) -> tuple[list[dict[str, Any]], list[str]]:
    records: list[dict[str, Any]] = []
    reasons: list[str] = []
    for name, relative_path, license_path, license_id in DIRECT_DEPENDENCIES:
        dependency = repo / relative_path
        expected_revision = EXPECTED_GITLINK_REVISIONS[name]
        actual_gitlink_revision = gitlink_revision(repo, relative_path)
        worktree_revision = run_git(dependency, "rev-parse", "HEAD") if dependency.is_dir() else None
        dirty = worktree_dirty(dependency) if worktree_revision else None
        license_file = dependency / license_path
        actual_license_hash = sha256_file(license_file) if license_file.is_file() else None
        expected_license_hash = EXPECTED_LICENSE_SHA256[name]
        if actual_gitlink_revision is None or worktree_revision is None:
            reasons.append(f"dependency:{name}:missing")
        else:
            if actual_gitlink_revision != expected_revision:
                reasons.append(f"dependency:{name}:gitlink-policy-mismatch")
            if worktree_revision != actual_gitlink_revision:
                reasons.append(f"dependency:{name}:revision-mismatch")
            if dirty is not False:
                reasons.append(f"dependency:{name}:worktree-state-not-clean")
        if actual_license_hash is None:
            reasons.append(f"dependency:{name}:license-file-missing")
        elif actual_license_hash != expected_license_hash:
            reasons.append(f"dependency:{name}:license-sha256-mismatch")
        records.append({
            "name": name,
            "path": relative_path,
            "usage": DEPENDENCY_USAGE[name],
            "gitlink_revision_expected": expected_revision,
            "gitlink_revision_actual": actual_gitlink_revision,
            "worktree_revision": worktree_revision,
            "worktree_state": "clean" if dirty is False else "not-clean" if dirty is True else "unavailable",
            "license_id": license_id,
            "license_file": license_path,
            "license_sha256_expected": expected_license_hash,
            "license_sha256_actual": actual_license_hash,
        })
    return records, reasons


def collect_provenance(repo: pathlib.Path, binary: pathlib.Path, build_mode: str, *, host_architecture: str | None, qualification_mode: bool = False, binary_version: str | None = None, architecture: str | None = None, needed: list[str] | None = None) -> dict[str, Any]:
    ava_version = project_version(repo)
    source_revision = run_git(repo, "rev-parse", "HEAD")
    source_dirty = worktree_dirty(repo)
    detected_architecture, detected_needed = elf_metadata(binary)
    architecture = architecture if architecture is not None else detected_architecture
    needed = needed if needed is not None else detected_needed
    host_architecture_matches_binary = (
        host_architecture == architecture
        if host_architecture is not None and architecture is not None
        else None
    )
    dependencies, reasons = dependency_records(repo)
    if ava_version is None:
        reasons.append("ava-version-unavailable")
    if binary_version != ava_version:
        reasons.append("binary-version-mismatch-or-unavailable")
    if source_revision is None:
        reasons.append("source-revision-unavailable")
    if source_dirty is not False:
        reasons.append("source-worktree-state-not-clean")
    if not qualification_mode:
        reasons.append("qualification-mode-not-requested")
    if build_mode != "source-build":
        reasons.append("build-mode-not-source-build")
    if host_architecture is None:
        reasons.append("host-architecture-missing")
    elif host_architecture not in QUALIFIED_ARCHITECTURES:
        reasons.append("host-architecture-unsupported")
    if architecture is None:
        reasons.append("binary-architecture-missing")
    elif architecture not in QUALIFIED_ARCHITECTURES:
        reasons.append("binary-architecture-unsupported")
    if host_architecture_matches_binary is False:
        reasons.append("host-binary-architecture-mismatch")
    dynamic_dependency_allowlist = ARCHITECTURE_DYNAMIC_ALLOWLISTS.get(architecture, HOST_DYNAMIC_ALLOWLIST)
    unexpected_dynamic_dependencies = None if needed is None else sorted(set(needed) - dynamic_dependency_allowlist)
    if needed is None:
        reasons.append("binary-not-elf")
    elif unexpected_dynamic_dependencies:
        reasons.append("unexpected-dynamic-dependency")
    license_ids = {record["name"]: record["license_id"] for record in dependencies}
    license_ids["boost_headers"] = "BSL-1.0"
    reasons = sorted(set(reasons))
    return {
        "schema_version": SCHEMA_VERSION,
        "ava_version": ava_version,
        "binary_version": binary_version,
        "binary_sha256": sha256_file(binary),
        "build_mode": build_mode,
        "qualification_mode": qualification_mode,
        "source": {"revision": source_revision, "worktree_state": "clean" if source_dirty is False else "not-clean" if source_dirty is True else "unavailable"},
        "direct_dependencies": dependencies + [{
            "name": "boost_headers", "path": "system headers", "usage": "header-only build dependency", "gitlink_revision_expected": None,
            "gitlink_revision_actual": None, "worktree_revision": None,
            "worktree_state": "not-applicable", "license_id": "BSL-1.0",
            "license_file": None, "license_sha256_expected": None, "license_sha256_actual": None,
        }],
        "license_ids": license_ids,
        "architecture": architecture,
        "host_architecture": host_architecture,
        "host_architecture_matches_binary": host_architecture_matches_binary,
        "elf_dt_needed": needed,
        "host_dynamic_dependency_allowlist": sorted(dynamic_dependency_allowlist),
        "unexpected_dynamic_dependencies": unexpected_dynamic_dependencies,
        "release_qualified": not reasons,
        "qualification_reasons": reasons,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=pathlib.Path)
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--build-mode", choices=("source-build", "supplied-binary"), required=True)
    parser.add_argument("--host-architecture", help="canonical architecture of the packaging host")
    parser.add_argument("--binary-version", help="exact version reported by the snapshotted binary")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--qualification-mode", action="store_true", help="evaluate and permit a qualification claim")
    parser.add_argument("--require-release-qualified", action="store_true")
    args = parser.parse_args()
    if not args.binary.is_file():
        parser.error(f"binary is not a regular file: {args.binary}")
    provenance = collect_provenance(
        args.repo, args.binary, args.build_mode,
        host_architecture=args.host_architecture,
        qualification_mode=args.qualification_mode or args.require_release_qualified,
        binary_version=args.binary_version,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.require_release_qualified and not provenance["release_qualified"]:
        unexpected = provenance["unexpected_dynamic_dependencies"]
        if unexpected:
            print("error: unexpected dynamic dependencies: " + ", ".join(unexpected), file=sys.stderr)
        print("error: release qualification failed: " + ", ".join(provenance["qualification_reasons"]), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
