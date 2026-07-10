#!/usr/bin/env python3
# print_members_coverage_test.py -- assert every ava header type opts into debug printing.
#
# Forces a refresh of the generated ctags tags.json by building the project's
# `ctags-json` target, so the scan always reflects the current source tree, then
# runs cmake/scripts/find_types_without_print_members.py against it and requires
# it to report zero types (structs and classes) missing an opt-in/opt-out marker.
#
# On failure the find script's full output is echoed to stdout so that ctest
# (with --output-on-failure) reports exactly which types still need the marker.
#
# The `ctags-json` target and tags.json only exist when libcwd debug support is
# enabled (OptionEnableLibcwd ON); this test is therefore registered only under
# that condition.
#
# Usage: print_members_coverage_test.py --source-dir <repo> --build-dir <build>
import argparse
import os
import subprocess
import sys


# The success marker emitted by find_types_without_print_members.py once every
# in-scope type has an opt-in or opt-out marker. The scan passes only when this
# exact substring appears in the script's combined output.
SUCCESS_MARKER = "0 struct(s), 0 class(es) missing an"

FIND_SCRIPT_REL = "cmake/scripts/find_types_without_print_members.py"
TAGS_JSON_REL = "generated/ctags/tags.json"


def run(argv):
    # Run a command, returning (returncode, combined_output). stdout and stderr
    # are merged so that ordering between the find script's stdout report and its
    # stderr summary is preserved for human readers.
    proc = subprocess.run(
        argv,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return proc.returncode, (proc.stdout or "")


def main(argv):
    parser = argparse.ArgumentParser(
        description="Require every ava header type to opt into debug printing.")
    parser.add_argument(
        "--source-dir",
        default=None,
        help="Repository root containing cmake/scripts/. "
             "Defaults to the REPOROOT environment variable.")
    parser.add_argument(
        "--build-dir",
        default=None,
        help="CMake build directory containing generated/ctags/tags.json. "
             "Defaults to the BUILDDIR environment variable.")
    args = parser.parse_args(argv)

    # Resolve source and build directories, allowing the planner-provided
    # REPOROOT/BUILDDIR environment variables as defaults so the script also runs
    # standalone from the developer shell.
    source_dir = args.source_dir
    if not source_dir:
        source_dir = os.environ.get("REPOROOT")
    build_dir = args.build_dir
    if not build_dir:
        build_dir = os.environ.get("BUILDDIR")
    if not source_dir:
        parser.error("--source-dir is required (or set REPOROOT).")
    if not build_dir:
        parser.error("--build-dir is required (or set BUILDDIR).")

    find_script = os.path.join(source_dir, FIND_SCRIPT_REL)
    tags_json = os.path.join(build_dir, TAGS_JSON_REL)

    # Refresh tags.json so the scan is based on the current sources rather than a
    # potentially stale file. `ctags-json` regenerates tags.json unconditionally
    # (generate_ctags_json.sh removes and rewrites it), which is what makes the
    # "up-to-date" guarantee. Failures here are surfaced to stdout before bailing.
    refresh_cmd = ["cmake", "--build", build_dir, "--target", "ctags-json"]
    rc, refresh_out = run(refresh_cmd)
    if rc != 0:
        sys.stdout.write(
            "Building the ctags-json target failed (exit {}); "
            "tags.json may be stale.\n".format(rc))
        sys.stdout.write(refresh_out)
        return 1

    # Run the scan, capturing its combined output. The script writes the
    # per-type report to stdout and the summary line to stderr; both are needed
    # to decide pass/fail and to report offenders on failure.
    scan_cmd = [sys.executable, find_script, tags_json]
    rc, scan_out = run(scan_cmd)
    if rc != 0:
        sys.stdout.write(
            "find_types_without_print_members.py exited {}.\n".format(rc))
        sys.stdout.write(scan_out)
        return 1

    if SUCCESS_MARKER not in scan_out:
        # One or more types are missing a marker. Print the full report so the
        # offending types are visible directly in the ctest failure output.
        sys.stdout.write(
            "Found struct(s)/class(es) in src/ava/ headers that are missing an "
            "AVA_DEBUG_PRINT_MEMBERS_ON / AVA_DEBUG_PRINT_MEMBERS_OPT_OUT "
            "marker:\n\n")
        sys.stdout.write(scan_out)
        return 1

    # All good: nothing to report. Keep the success line quiet to avoid noise,
    # but confirm the test ran its checks.
    sys.stdout.write(
        "All in-scope ava header types have a print_members opt-in/opt-out "
        "marker (0 structs, 0 classes missing).\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
