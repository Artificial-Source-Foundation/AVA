#!/usr/bin/env python3
# generate_print_members_rules_test.py -- exercise the print_members build rules.
#
# Runs a fixed sequence of cmake invocations against the project build tree,
# captures each step's argv, exit status, stdout, stderr, and elapsed time, and
# checks the combined output against a fixed set of regular expressions. For every step
# that carries an expectation, the step passes only when its exit status is zero
# AND exactly the expected substrings are present (no missing, no unexpected).
# Steps without an expectation (the warm-up `make` and the `touch`/`rm` probes)
# only require a zero exit status; they still report which substrings turned up
# so the actual behavior is visible.
#
# Command translation (matches the planner's environment helpers):
#   make                        -> cmake --build "$BUILDDIR" --parallel 16
#   make ctags-json             -> cmake --build "$BUILDDIR" --target ctags-json
#   make generate-print-members -> cmake --build "$BUILDDIR" --parallel 16 -- generate-print-members
#   configure                   -> cmake -S "$REPOROOT" -B "$BUILDDIR" -DCMAKE_BUILD_TYPE=Debug
#                                  -GNinja --log-level=DEBUG -DCMAKE_VERBOSE_MAKEFILE=ON
#                                  -DCMAKE_MESSAGE_LOG_LEVEL=DEBUG
#                                  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
#                                  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DAVA_BUILD_TESTS=ON
#
# `touch` and `rm` probes run unchanged. Environment variables REPOROOT and
# BUILDDIR must be set (the planner exposes them); the script fails early with a
# clear message otherwise.

import os
import re
import subprocess
import sys
import time


# Regular expressions scanned for in each step's combined stdout+stderr, matched
# with re.search (unanchored, single-line: '.' does not cross newlines). The
# integer key is the label used in the per-step expectation table below.
#
# The patterns are deliberately specific so each one only matches the line it is
# meant to assert on:
#  - #8 is anchored to the configure-time CMake status prefix ("-- Generating")
#    so it does NOT match the build-time `ctags-json` target COMMENT ("Generating
#    JSON tags for debug printer generation: .../generated/ctags/tags.json") nor
#    the verbose command echo that merely passes the path as an argument.
#  - #7 wraps the literal "(re)" in a real (non-capturing, optional) group so it
#    matches both "Need generation ..." and "Need (re)generation ...". Writing
#    "\(re)?" would be wrong: the "?" would only make the closing paren optional,
#    leaving "(" and "re" required.
#  - #4/#5 require the ".cpp.o" object suffix so they match compile lines only,
#    not the configure "Need (re)generation ... <path>/ava/config/print_members.cpp"
#    status message that merely echoes the missing file path.
_RAW_PATTERNS = {
    1: r"ninja: no work to do",
    2: r"scripts/generate_ctags_json\.sh",
    3: r"scripts/generate_print_members\.py",
    4: r"-o [^ ]*ava/agent/print_members\.cpp\.o",
    5: r"-o [^ ]*ava/config/print_members\.cpp\.o",
    6: r"Not regenerating print_members\.cpp files",
    7: r"Need (?:\(re\))?generation of print_members\.cpp files because",
    8: r"-- Generating .*/generated/ctags/tags\.json",
    9: r"Generating print_members\.cpp files",
}
PATTERNS = {key: re.compile(pat) for key, pat in _RAW_PATTERNS.items()}


def required_env(name):
    # Fail loudly when a planner-provided variable is missing rather than
    # silently building against the wrong tree.
    value = os.environ.get(name)
    if not value:
        sys.stderr.write(
            "generate_print_members_rules_test.py: environment variable {} is not set\n".format(name)
        )
        sys.exit(2)
    return value


def found_substrings(text):
    # Return the frozenset of PATTERNS keys whose compiled regex matches
    # anywhere in the combined output. A pattern counts as found if any output
    # line matches it.
    return frozenset(key for key, pat in PATTERNS.items() if pat.search(text))


def fmt_set(indices):
    # Render a set of substring indices as "[3, 4, 5]" for compact reporting.
    return "[" + ", ".join(str(i) for i in sorted(indices)) + "]"


def main(argv):
    repo_root = required_env("REPOROOT")
    build_dir = required_env("BUILDDIR")

    # Build the four command kinds the sequence uses. Each helper returns a
    # fresh argv list so nothing is mutated by accident between steps.
    def make():
        return ["cmake", "--build", build_dir, "--parallel", "16"]

    def make_ctags_json():
        return ["cmake", "--build", build_dir, "--target", "ctags-json"]

    def make_generate_print_members():
        return ["cmake", "--build", build_dir, "--parallel", "16", "--", "generate-print-members"]

    def configure():
        return [
            "cmake", "-S", repo_root, "-B", build_dir,
            "-DCMAKE_BUILD_TYPE=Debug",
            "-GNinja",
            "--log-level=DEBUG",
            "-DCMAKE_VERBOSE_MAKEFILE=ON",
            "-DCMAKE_MESSAGE_LOG_LEVEL=DEBUG",
            "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache",
            "-DCMAKE_C_COMPILER_LAUNCHER=ccache",
            "-DAVA_BUILD_TESTS=ON",
        ]

    # Probed generated files. The absolute paths in the original run transcript
    # ($BUILDDIR/generated/...) are reconstructed from $BUILDDIR here so the
    # script works in any checkout.
    print_members_cpp = os.path.join(
        build_dir, "generated", "print_members", "ava", "config", "print_members.cpp"
    )
    source_files = os.path.join(build_dir, "generated", "print_members", "source_files")

    # Each step is (label, argv, comment, expected). `expected` is either a
    # frozenset of PATTERNS keys that must be exactly the ones found, or None
    # for steps that only need a zero exit status (no substring assertion). The
    # comment documents the rule behavior the step exercises; it is not used for
    # any assertion. The expectation table follows the comment->substring map:
    #   ninja: no work to do                       -> {1}
    #   runs generate_ctags_json.sh                -> {2}
    #   run generate_print_members.py + compile all-> {3, 4, 5}
    #   runs .sh then .py (generate-print-members) -> {2, 3}
    #   recompiles all print_members.cpp, no .py   -> {4, 5}
    #   only recompiles ava/config/print_members   -> {5}
    #   configure: Not regenerating                -> {6}
    #   configure: Need (re)generation             -> {7, 8, 9}
    steps = [
        ("make", make(),
         "initial build (no substring expectation)",
         None),
        ("make", make(),
         "ninja: no work to do.",
         frozenset({1})),
        ("make ctags-json", make_ctags_json(),
         "runs generate_ctags_json.sh",
         frozenset({2})),
        ("make ctags-json", make_ctags_json(),
         "runs generate_ctags_json.sh again",
         frozenset({2})),
        ("make", make(),
         "run generate_print_members.py - and then compiles all print_members.cpp (and relinks)",
         frozenset({3, 4, 5})),
        ("make", make(),
         "ninja: no work to do.",
         frozenset({1})),
        ("make generate-print-members", make_generate_print_members(),
         "runs generate_ctags_json.sh and then generate_print_members.py",
         frozenset({2, 3})),
        ("make generate-print-members", make_generate_print_members(),
         "runs generate_ctags_json.sh and then generate_print_members.py",
         frozenset({2, 3})),
        ("make", make(),
         "does NOT run any .py script, just recompiles all print_members.cpp (and relinks)",
         frozenset({4, 5})),
        ("make", make(),
         "ninja: no work to do.",
         frozenset({1})),
        ("touch", ["touch", print_members_cpp],
         "mark ava/config/print_members.cpp stale",
         None),
        ("make", make(),
         "only recompiles ava/config/print_members.cpp (and relinks)",
         frozenset({5})),
        ("touch", ["touch", print_members_cpp],
         "mark ava/config/print_members.cpp stale again",
         None),
        ("configure", configure(),
         "prints: -- Not regenerating print_members.cpp files",
         frozenset({6})),
        ("rm", ["rm", print_members_cpp],
         "delete ava/config/print_members.cpp",
         None),
        ("configure", configure(),
         'prints: -- Need (re)generation of print_members.cpp files because '
         '".../ava/config/print_members.cpp" doesn\'t exist.\n'
         "-- Generating .../generated/ctags/tags.json\n"
         "-- Generating print_members.cpp files in .../generated/print_members",
         frozenset({7, 8, 9})),
        ("make", make(),
         "does NOT run any .py script, just recompiles all print_members.cpp (and relinks)",
         frozenset({4, 5})),
        ("configure", configure(),
         "prints: -- Not regenerating print_members.cpp files",
         frozenset({6})),
        ("rm", ["rm", source_files],
         "delete the source_files manifest",
         None),
        ("make", make(),
         "ninja: no work to do.",
         frozenset({1})),
        ("configure", configure(),
         "prints: -- Need generation of print_members.cpp files because "
         ".../generated/print_members/source_files doesn't exist.\n"
         "-- Generating .../generated/ctags/tags.json\n"
         "-- Generating print_members.cpp files in .../generated/print_members",
         frozenset({7, 8, 9})),
    ]

    # Run the sequence, capturing everything. Steps run to completion regardless
    # of prior failures so a full record is always collected; per-step pass/fail
    # is determined by exit status plus the substring expectation below.
    records = []
    failed_steps = []
    for index, (label, argv, comment, expected) in enumerate(steps, start=1):
        header = "{}  # {}".format(label, comment) if comment else label
        print("[{:02}/{}] {}".format(index, len(steps), header))
        print("       $ " + " ".join(argv))
        start = time.monotonic()
        proc = subprocess.run(argv, capture_output=True, text=True)
        elapsed = time.monotonic() - start

        combined = (proc.stdout or "") + (proc.stderr or "")
        found = found_substrings(combined)

        rc_ok = proc.returncode == 0
        if expected is None:
            # Lenient step: only the exit status matters; report found substrings
            # for visibility but never fail on them.
            match_ok = True
            expectation_desc = "no substring expectation; found {}".format(fmt_set(found))
        else:
            missing = expected - found
            unexpected = found - expected
            match_ok = (not missing) and (not unexpected)
            expectation_desc = "expected {} found {}".format(
                fmt_set(expected), fmt_set(found)
            )
            if missing:
                expectation_desc += "; missing {}".format(fmt_set(missing))
            if unexpected:
                expectation_desc += "; unexpected {}".format(fmt_set(unexpected))

        step_ok = rc_ok and match_ok
        record = {
            "index": index,
            "label": label,
            "comment": comment,
            "argv": list(argv),
            "expected": expected,
            "found": found,
            "returncode": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
            "elapsed": elapsed,
            "ok": step_ok,
        }
        records.append(record)

        rc_status = "ok" if rc_ok else "FAIL(rc={})".format(proc.returncode)
        verdict = "PASS" if step_ok else "FAIL"
        print("       -> {} ({:.2f}s) | {}".format(rc_status, elapsed, expectation_desc))
        print("       == {}".format(verdict))
        if not step_ok:
            failed_steps.append(index)
            # On any failure, surface the captured output so a manual run can see
            # what went wrong without re-running with extra instrumentation.
            if proc.stdout:
                print("       --- stdout ---")
                for line in proc.stdout.splitlines():
                    print("       " + line)
            if proc.stderr:
                print("       --- stderr ---")
                for line in proc.stderr.splitlines():
                    print("       " + line)

    print("\n{} steps run, {} passed, {} failed".format(
        len(records), len(records) - len(failed_steps), len(failed_steps)))
    if failed_steps:
        print("failed steps: {}".format(fmt_set(failed_steps)))
    return 1 if failed_steps else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
