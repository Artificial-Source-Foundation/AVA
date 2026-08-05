#!/usr/bin/env python3
import argparse
import fcntl
import os
import pathlib
import pty
import re
import select
import shutil
import signal
import struct
import subprocess
import sys
import termios
import time


SKIP = 77
MAX_CAPTURE_BYTES = 2 * 1024 * 1024
ENVIRONMENT_ALLOWLIST = (
    "PATH",
    "LANG",
    "LANGUAGE",
    "LC_ALL",
    "LC_CTYPE",
    "LC_MESSAGES",
    "LC_NUMERIC",
    "LC_TIME",
    "LC_COLLATE",
    "LC_MONETARY",
    "TERMINFO",
    "TERMINFO_DIRS",
    "LD_LIBRARY_PATH",
    "DYLD_LIBRARY_PATH",
    "DYLD_FALLBACK_LIBRARY_PATH",
    "ASAN_OPTIONS",
    "UBSAN_OPTIONS",
    "LSAN_OPTIONS",
    "TSAN_OPTIONS",
    "MSAN_OPTIONS",
    "HWASAN_OPTIONS",
    "ASAN_SYMBOLIZER_PATH",
    "UBSAN_SYMBOLIZER_PATH",
    "LSAN_SYMBOLIZER_PATH",
    "TSAN_SYMBOLIZER_PATH",
    "MSAN_SYMBOLIZER_PATH",
    "HWASAN_SYMBOLIZER_PATH",
    "LLVM_SYMBOLIZER_PATH",
    "TMPDIR",
    "TZ",
    "AVA_TEST_NAME",
    "AVA_DEBUG_OUTPUT_DIR",
    "LIBCWD_RCFILE_NAME",
    "LIBCWD_RCFILE_OVERRIDE_NAME",
)


def enabled(value: str | None) -> bool:
    return value is not None and value.lower() in {"1", "true", "yes", "on"}


def set_winsize(fd: int, rows: int, cols: int) -> None:
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def allowlisted_environment() -> dict[str, str]:
    env = {key: os.environ[key] for key in ENVIRONMENT_ALLOWLIST if key in os.environ}
    # Match the libcwd suppression applied to ava_tests.*: this sealed env does
    # not inherit os.environ, so set the pair explicitly to keep ava's debug
    # initialization from writing to the streams this smoke inspects.
    env["LIBCWD_NO_STARTUP_MSGS"] = "1"
    env["AVA_NO_DEBUG_OUTPUT"] = "1"
    return env


def strip_csi(text: bytes) -> bytes:
    out = bytearray()
    index = 0
    while index < len(text):
        if text[index:index + 2] == b"\x1b[":
            index += 2
            while index < len(text) and not (0x40 <= text[index] <= 0x7E):
                index += 1
            if index < len(text):
                index += 1
            continue
        if text[index:index + 2] == b"\x1b]":
            index += 2
            while index < len(text):
                if text[index:index + 2] == b"\x1b\\":
                    index += 2
                    break
                if text[index] == 0x07:
                    index += 1
                    break
                index += 1
            continue
        out.append(text[index])
        index += 1
    return bytes(out)


def read_until(master_fd: int, process: subprocess.Popen[bytes], predicate, label: str, timeout: float = 10.0) -> bytes:
    deadline = time.monotonic() + timeout
    captured = bytearray()
    while time.monotonic() < deadline:
        if predicate(bytes(captured)):
            return bytes(captured)
        ready, _, _ = select.select([master_fd], [], [], 0.1)
        if ready:
            try:
                chunk = os.read(master_fd, 8192)
            except OSError:
                chunk = b""
            if chunk:
                captured.extend(chunk)
                if len(captured) > MAX_CAPTURE_BYTES:
                    raise RuntimeError(f"terminal output exceeded {MAX_CAPTURE_BYTES} bytes while waiting for {label}")
                continue
        if process.poll() is not None:
            raise RuntimeError(
                f"AVA exited before {label} with code {process.returncode}\n"
                f"captured:\n{strip_csi(bytes(captured)).decode(errors='replace')}"
            )
    raise RuntimeError(
        f"timed out waiting for {label}\n"
        f"captured:\n{strip_csi(bytes(captured)).decode(errors='replace')}"
    )


def drain_exit(master_fd: int, process: subprocess.Popen[bytes], timeout: float = 5.0) -> tuple[int, bytes]:
    deadline = time.monotonic() + timeout
    captured = bytearray()
    saw_eof = False
    while time.monotonic() < deadline:
        ready, _, _ = select.select([master_fd], [], [], 0.1)
        if ready:
            try:
                chunk = os.read(master_fd, 8192)
            except OSError:
                chunk = b""
            if chunk:
                captured.extend(chunk)
                if len(captured) > MAX_CAPTURE_BYTES:
                    raise RuntimeError(f"terminal teardown exceeded {MAX_CAPTURE_BYTES} bytes")
            else:
                saw_eof = True
        returncode = process.poll()
        if returncode is not None and saw_eof:
            return returncode, bytes(captured)
        if returncode is not None and not ready:
            try:
                chunk = os.read(master_fd, 8192)
            except OSError:
                chunk = b""
            if chunk:
                captured.extend(chunk)
            else:
                return returncode, bytes(captured)
    raise RuntimeError(f"timed out draining AVA exit; captured {len(captured)} terminal bytes")


def process_group_exists(pgid: int) -> bool:
    try:
        os.killpg(pgid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def wait_for_no_process_group(pgid: int, timeout: float = 1.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not process_group_exists(pgid):
            return
        select.select([], [], [], min(0.05, max(0.0, deadline - time.monotonic())))
    raise RuntimeError(f"AVA process group {pgid} remained after exit")


def terminate_process(process: subprocess.Popen[bytes], master_fd: int) -> None:
    if process.poll() is None:
        try:
            os.write(master_fd, b"\x04")
            process.wait(timeout=3)
        except Exception:
            pass
    if process_group_exists(process.pid):
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline and process_group_exists(process.pid):
            process.poll()
            select.select([], [], [], 0.05)
        if process_group_exists(process.pid):
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ava", required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--protocol", choices=("kitty", "iterm2"), default="kitty")
    args = parser.parse_args()

    gate = "AVA_TUI_KITTY_IMAGE_SMOKE" if args.protocol == "kitty" else "AVA_TUI_ITERM2_IMAGE_SMOKE"
    if not enabled(os.environ.get(gate)):
        print(f"skipping {args.protocol} image PTY smoke; set {gate}=1 to run")
        return SKIP

    ava_exe = pathlib.Path(args.ava).absolute()
    if not ava_exe.exists():
        raise RuntimeError(f"AVA executable does not exist: {ava_exe}")

    root = pathlib.Path(args.root).absolute()
    expected_root_name = "tui-kitty-image-smoke" if args.protocol == "kitty" else "tui-iterm2-image-smoke"
    if root.exists():
        if root.name != expected_root_name:
            raise RuntimeError(f"refusing to clear unexpected smoke root: {root}")
        shutil.rmtree(root)
    root.mkdir(parents=True, mode=0o700)
    workspace = root / "workspace"
    home = root / "home"
    config = root / "config"
    state = root / "state"
    data = root / "data"
    cache = root / "cache"
    runtime = root / "runtime"
    for path in (workspace, home, config, state, data, cache, runtime):
        path.mkdir(parents=True, exist_ok=True)
    runtime.chmod(0o700)
    (workspace / "AGENTS.md").write_text(f"{args.protocol} image smoke context\n", encoding="utf-8")
    (workspace / "screen.png").write_bytes(b"\x89PNG\r\n\x1a\nava-kitty-image")

    env = allowlisted_environment()
    env.update(
        {
            "HOME": str(home),
            "XDG_CONFIG_HOME": str(config),
            "XDG_STATE_HOME": str(state),
            "XDG_DATA_HOME": str(data),
            "XDG_CACHE_HOME": str(cache),
            "XDG_RUNTIME_DIR": str(runtime),
            "TERM": "xterm-256color",
            "COLORTERM": "truecolor",
        }
    )
    for key in (
        "TMUX", "TMUX_PANE", "NO_COLOR", "AVA_TUI_THEME", "KITTY_WINDOW_ID", "GHOSTTY_RESOURCES_DIR",
        "WEZTERM_PANE", "WARP_SESSION_ID", "WARP_TERMINAL_SESSION_UUID", "ITERM_SESSION_ID", "WT_SESSION",
    ):
        env.pop(key, None)
    if args.protocol == "kitty":
        env.update({"TERM_PROGRAM": "kitty", "KITTY_WINDOW_ID": "1"})
    else:
        env.update({"TERM_PROGRAM": "iTerm.app", "ITERM_SESSION_ID": "w0t0p0:smoke"})

    master_fd, slave_fd = pty.openpty()
    set_winsize(slave_fd, 28, 100)
    process = subprocess.Popen(
        [str(ava_exe)],
        cwd=workspace,
        env=env,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        start_new_session=True,
    )
    os.close(slave_fd)

    try:
        read_until(master_fd, process, lambda data: b"Type a message" in data or b"live session" in data,
                   "initial TUI frame")
        os.write(master_fd, b"/attach screen.png\r")
        expected_payload = b"iVBORw0KGgphdmEta2l0dHktaW1hZ2U="
        if args.protocol == "kitty":
            emitted = lambda data: b"\x1b_Ga=T,f=100,q=2,C=1" in data and expected_payload in data and b"\x1b\\" in data
            emission_label = "Kitty graphics emission"
        else:
            emitted = lambda data: b"\x1b]1337;File=inline=1" in data and expected_payload + b"\a" in data
            emission_label = "iTerm2 inline-image emission"
        captured = read_until(master_fd, process, emitted, emission_label)
        visible = strip_csi(captured)
        expected_preview = f"preview {args.protocol}".encode()
        if b"attached image" not in visible or b"screen.png" not in visible or expected_preview not in visible:
            raise RuntimeError(
                f"attached image metadata did not remain visible with {args.protocol} preview\n"
                f"captured:\n{visible.decode(errors='replace')}"
            )
        if expected_payload not in captured:
            raise RuntimeError(f"{args.protocol} graphics sequence did not include the smoke PNG payload")

        image_id = None
        if args.protocol == "kitty":
            transmit = re.search(rb"\x1b_Ga=T,[^;]*\bi=([0-9]+)(?:,|;)", captured)
            if transmit is None:
                raise RuntimeError("Kitty graphics sequence did not include a bounded active image ID")
            image_id = int(transmit.group(1))

        os.write(master_fd, b"\x04")
        returncode, teardown = drain_exit(master_fd, process)
        if returncode != 0:
            raise RuntimeError(f"AVA clean exit returned {returncode}")
        if args.protocol == "kitty":
            delete = f"\x1b_Ga=d,d=I,i={image_id},q=2\x1b\\".encode()
            delete_at = teardown.find(delete)
            keyboard_pop_at = teardown.find(b"\x1b[<u")
            if delete_at < 0:
                raise RuntimeError(
                    f"active Kitty image {image_id} was not deleted during clean exit; teardown={teardown!r}"
                )
            if keyboard_pop_at < 0 or delete_at > keyboard_pop_at:
                raise RuntimeError(
                    "Kitty image delete did not precede keyboard-protocol teardown; "
                    f"delete_at={delete_at} keyboard_pop_at={keyboard_pop_at}"
                )
            print(
                f"kitty: image_id={image_id} delete_at={delete_at} keyboard_pop_at={keyboard_pop_at} "
                f"exit={returncode}"
            )
        else:
            print(f"iterm2: osc1337_base64_bytes={len(expected_payload)} terminator=BEL exit={returncode}")
        wait_for_no_process_group(process.pid)
        return 0
    finally:
        terminate_process(process, master_fd)
        os.close(master_fd)


if __name__ == "__main__":
    sys.exit(main())
