#!/usr/bin/env python3
"""Behavioral regressions for the bounded live ACP dogfood launcher."""

import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile


ZED_COMMIT = "0" * 40
EXPECTED_ZED_FILE_LIMIT = 64 * 1024 * 1024


def write_executable(path, body):
    path.write_text(body, encoding="utf-8")
    path.chmod(0o700)


def clean_environment(root):
    return {
        "HOME": str(root / "launcher-home"),
        "PATH": "/usr/bin:/bin",
        "TMPDIR": str(root),
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
        # Match the libcwd suppression applied to ava_tests.*: this clean env
        # does not inherit os.environ, so set the pair explicitly to keep ava's
        # debug initialization from writing to the streams this test inspects.
        "LIBCWD_NO_STARTUP_MSGS": "1",
        "AVA_NO_DEBUG_OUTPUT": "1",
    }


def run_launcher(script, arguments, root, stdin=""):
    (root / "launcher-home").mkdir(exist_ok=True)
    return subprocess.run(
        [str(script), *arguments],
        input=stdin,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=30,
        check=False,
        env=clean_environment(root),
    )


def common_arguments(ava, provider, zed, root_parent, preflight):
    return [
        "zed",
        "run",
        "--ava",
        str(ava),
        "--fake-provider",
        str(provider),
        "--zed",
        str(zed),
        "--acknowledge-dedicated-display",
        "--confinement",
        "sandbox",
        "--confinement-description",
        "isolated launcher regression test",
        "--preflight-evidence",
        str(preflight),
        "--root-parent",
        str(root_parent),
        "--operator-timeout-seconds",
        "60",
    ]


def assert_display_validation(script, root):
    socket_path = root / "wayland-test"
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
        listener.bind(str(socket_path))
        listener.listen(1)
        preflight = root / "preflight.txt"
        preflight.write_text("reviewed test boundary\n", encoding="utf-8")
        base = common_arguments(Path("/bin/true"), Path("/bin/true"), Path("/bin/true"), root, preflight)

        both = run_launcher(script, [*base, "--display", ":98", "--wayland-display", str(socket_path)], root)
        assert both.returncode != 0
        assert "declare exactly one of --display or --wayland-display" in both.stderr

        authority = root / "authority"
        authority.write_text("test\n", encoding="utf-8")
        wayland_with_xauth = run_launcher(
            script,
            [*base, "--wayland-display", str(socket_path), "--display-auth-file", str(authority)],
            root,
        )
        assert wayland_with_xauth.returncode != 0
        assert "--display-auth-file is valid only with --display" in wayland_with_xauth.stderr

        socket_link = root / "wayland-link"
        socket_link.symlink_to(socket_path)
        linked = run_launcher(script, [*base, "--wayland-display", str(socket_link)], root)
        assert linked.returncode != 0
        assert "absolute non-symlink socket path" in linked.stderr

        valid = run_launcher(script, [*base, "--wayland-display", str(socket_path)], root)
        assert valid.returncode != 0
        assert "--wayland-display must" not in valid.stderr
        assert "could not parse a Zed version" in valid.stderr


def assert_startup_and_file_limit(script, root):
    ava = Path("/bin/true")
    provider = root / "fake-provider.py"
    write_executable(
        provider,
        """#!/usr/bin/python3
import os
from pathlib import Path
import signal
import sys
port = Path(sys.argv[1])
port.write_text("12345\\n", encoding="utf-8")
port.with_name("provider-pid").write_text(f"{os.getpid()}\\n", encoding="utf-8")
signal.pause()
""",
    )
    preflight = root / "preflight.txt"
    preflight.write_text("reviewed test boundary\n", encoding="utf-8")

    exit_zed = root / "zed-exit.py"
    write_executable(
        exit_zed,
        f"""#!/usr/bin/python3
import sys
if len(sys.argv) > 1 and sys.argv[1] == "--version":
    print("Zed 1.9.0 {ZED_COMMIT}")
    raise SystemExit(0)
raise SystemExit(0)
""",
    )
    exit_root = root / "exit-root"
    exit_root.mkdir()
    exited = run_launcher(
        script,
        [*common_arguments(ava, provider, exit_zed, exit_root, preflight), "--display", ":98"],
        root,
    )
    assert exited.returncode != 0
    assert "exited or became a zombie during startup" in exited.stderr
    assert "raw/zed.stderr" in exited.stderr

    limit_file = root / "zed-limit.txt"
    limit_zed = root / "zed-limit.py"
    write_executable(
        limit_zed,
        f"""#!/usr/bin/python3
from pathlib import Path
import resource
import signal
import sys
import time
if len(sys.argv) > 1 and sys.argv[1] == "--version":
    print("Zed 1.9.0 {ZED_COMMIT}")
    raise SystemExit(0)
with Path({str(limit_file)!r}).open("a", encoding="utf-8") as output:
    output.write(f"{{resource.getrlimit(resource.RLIMIT_FSIZE)[0]}}\\n")
def stop(_signal, _frame):
    print("tail-on-term", flush=True)
    raise SystemExit(0)
signal.signal(signal.SIGTERM, stop)
print("capture-started", flush=True)
time.sleep(30)
""",
    )
    limit_root = root / "limit-root"
    limit_root.mkdir()
    limited = run_launcher(
        script,
        [*common_arguments(ava, provider, limit_zed, limit_root, preflight), "--display", ":98"],
        root,
        stdin="incomplete\n",
    )
    assert limited.returncode != 0
    limits = [int(value) for value in limit_file.read_text(encoding="utf-8").splitlines()]
    assert limits == [EXPECTED_ZED_FILE_LIMIT, EXPECTED_ZED_FILE_LIMIT], limits

    for run_root in (exit_root, limit_root):
        phase_roots = list(run_root.glob("ava-zed-dogfood.*/phases/*"))
        assert phase_roots
        for phase_root in phase_roots:
            for log_name in ("zed.stdout", "zed.stderr"):
                log = phase_root / "raw" / log_name
                if log.exists():
                    assert log.stat().st_size <= 16 * 1024 * 1024
            if run_root == limit_root:
                assert "tail-on-term" in (phase_root / "raw/zed.stdout").read_text(encoding="utf-8")

    capped_zed = root / "zed-capped.py"
    write_executable(
        capped_zed,
        f"""#!/usr/bin/python3
import sys
import time
if len(sys.argv) > 1 and sys.argv[1] == "--version":
    print("Zed 1.9.0 {ZED_COMMIT}")
    raise SystemExit(0)
sys.stdout.write("x" * (17 * 1024 * 1024))
sys.stdout.flush()
time.sleep(30)
""",
    )
    capped_root = root / "capped-root"
    capped_root.mkdir()
    capped = run_launcher(
        script,
        [*common_arguments(ava, provider, capped_zed, capped_root, preflight), "--display", ":98"],
        root,
    )
    assert capped.returncode != 0
    assert "capture exited nonzero" in capped.stderr
    capped_logs = list(capped_root.glob("ava-zed-dogfood.*/phases/*/raw/zed.stdout"))
    assert capped_logs and all(log.stat().st_size <= 16 * 1024 * 1024 for log in capped_logs)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    args = parser.parse_args()
    source = Path(args.source).absolute()
    script = source / "scripts/live-acp-dogfood.sh"
    assert script.is_file() and os.access(script, os.X_OK)

    with tempfile.TemporaryDirectory(prefix="ava-live-acp-dogfood-test-") as temporary:
        root = Path(temporary).absolute()
        display_root = root / "display"
        display_root.mkdir()
        assert_display_validation(script, display_root)
        startup_root = root / "startup"
        startup_root.mkdir()
        assert_startup_and_file_limit(script, startup_root)

    print("live ACP dogfood launcher display, startup, and file-limit tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
