#!/usr/bin/env python3
import argparse
import fcntl
import os
import pathlib
import pty
import select
import shutil
import signal
import struct
import subprocess
import sys
import termios
import time


SKIP = 77


def enabled(value: str | None) -> bool:
    return value is not None and value.lower() in {"1", "true", "yes", "on"}


def set_winsize(fd: int, rows: int, cols: int) -> None:
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def strip_control_sequences(text: bytes) -> bytes:
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
                continue
        if process.poll() is not None:
            raise RuntimeError(
                f"AVA exited before {label} with code {process.returncode}\n"
                f"captured:\n{strip_control_sequences(bytes(captured)).decode(errors='replace')}"
            )
    raise RuntimeError(
        f"timed out waiting for {label}\n"
        f"captured:\n{strip_control_sequences(bytes(captured)).decode(errors='replace')}"
    )


def terminate_process(process: subprocess.Popen[bytes], master_fd: int) -> None:
    if process.poll() is not None:
        return
    try:
        os.write(master_fd, b"\x04")
        process.wait(timeout=3)
        return
    except Exception:
        pass
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=3)


def wait_for_file(path: pathlib.Path, process: subprocess.Popen[bytes], label: str, timeout: float = 8.0) -> None:
    deadline = time.monotonic() + timeout
    while not path.exists():
        if process.poll() is not None:
            raise RuntimeError(f"{label} exited before creating {path}")
        if time.monotonic() >= deadline:
            raise RuntimeError(f"timed out waiting for {label} to create {path}")
        time.sleep(0.05)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ava", required=True)
    parser.add_argument("--fake-provider", required=True)
    parser.add_argument("--root", required=True)
    args = parser.parse_args()

    if not enabled(os.environ.get("AVA_TUI_OSC8_SMOKE")):
        print("skipping OSC 8 PTY smoke; set AVA_TUI_OSC8_SMOKE=1 to run")
        return SKIP

    ava_exe = pathlib.Path(args.ava).resolve()
    fake_provider_exe = pathlib.Path(args.fake_provider).resolve()
    if not ava_exe.exists():
        raise RuntimeError(f"AVA executable does not exist: {ava_exe}")
    if not fake_provider_exe.exists():
        raise RuntimeError(f"fake provider executable does not exist: {fake_provider_exe}")

    root = pathlib.Path(args.root).resolve()
    if root.exists():
        if root.name != "tui-osc8-smoke":
            raise RuntimeError(f"refusing to clear unexpected smoke root: {root}")
        shutil.rmtree(root)
    workspace = root / "workspace"
    home = root / "home"
    config = root / "config"
    state = root / "state"
    data = root / "data"
    ava_config = config / "ava"
    for path in (workspace, home, ava_config, state, data):
        path.mkdir(parents=True, exist_ok=True)
    (workspace / "AGENTS.md").write_text("osc8 smoke context\n", encoding="utf-8")
    (ava_config / "models.json").write_text(
        '{"default_provider":"moonshot","default_model":"ava-osc8-fake",'
        '"models":[{"provider":"moonshot","id":"ava-osc8-fake","name":"AVA OSC8 Fake","family":"fake",'
        '"context_window_tokens":8192,"max_output_tokens":1024,"supports_tools":false,'
        '"supports_streaming":false,"supports_reasoning":false,"reports_usage":true}]}\n',
        encoding="utf-8",
    )

    port_file = root / "provider.port"
    request_log = root / "provider-requests.log"
    provider = subprocess.Popen(
        [
            str(fake_provider_exe),
            str(port_file),
            str(request_log),
            "0",
            "markdown-links",
            "",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    master_fd = -1
    try:
        wait_for_file(port_file, provider, "fake provider")
        port = port_file.read_text(encoding="utf-8").strip()

        env = os.environ.copy()
        env.update(
            {
                "HOME": str(home),
                "XDG_CONFIG_HOME": str(config),
                "XDG_STATE_HOME": str(state),
                "XDG_DATA_HOME": str(data),
                "TERM": "xterm-256color",
                "TERM_PROGRAM": "vscode",
                "COLORTERM": "truecolor",
                "MOONSHOT_API_KEY": "test-key",
                "MOONSHOT_BASE_URL": f"http://127.0.0.1:{port}",
            }
        )
        for key in (
            "TMUX",
            "TMUX_PANE",
            "NO_COLOR",
            "AVA_TUI_THEME",
            "KITTY_WINDOW_ID",
            "GHOSTTY_RESOURCES_DIR",
            "WEZTERM_PANE",
            "WARP_SESSION_ID",
            "WARP_TERMINAL_SESSION_UUID",
            "ITERM_SESSION_ID",
            "WT_SESSION",
        ):
            env.pop(key, None)

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
            os.write(master_fd, b"show markdown links\r")
            captured = read_until(
                master_fd,
                process,
                lambda data: (
                    b"\x1b]8;;https://e.test/d\x1b\\" in data
                    and b"\x1b]8;;https://e.test/b\x1b\\" in data
                    and b"\x1b]8;;mailto:u@e.test\x1b\\" in data
                    and b"\x1b]8;;\x1b\\" in data
                ),
                "OSC 8 assistant Markdown links",
                timeout=12.0,
            )
            visible = strip_control_sequences(captured)
            if b"Docs" not in visible or b"https://e.test/b" not in visible or b"u@e.test" not in visible:
                raise RuntimeError(
                    "assistant Markdown link text was not visible while OSC 8 links were emitted\n"
                    f"captured:\n{visible.decode(errors='replace')}"
                )
            if b"(https://e.test/d)" in visible:
                raise RuntimeError(
                    "OSC 8 hyperlink mode leaked the visible URL fallback for a descriptive Markdown link\n"
                    f"captured:\n{visible.decode(errors='replace')}"
                )
            os.write(master_fd, b"\x04")
            process.wait(timeout=5)
            return 0
        finally:
            terminate_process(process, master_fd)
    finally:
        if master_fd >= 0:
            os.close(master_fd)
        if provider.poll() is None:
            provider.terminate()
            try:
                provider.wait(timeout=3)
            except subprocess.TimeoutExpired:
                provider.kill()
                provider.wait(timeout=3)


if __name__ == "__main__":
    sys.exit(main())
