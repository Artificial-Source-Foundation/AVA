#!/usr/bin/env python3
"""Bounded, credential-free opt-in smoke for the pinned acpx CLI and AVA."""

import argparse
import json
import os
from pathlib import Path
import shutil
import shlex
import signal
import subprocess
import sys
import tempfile
import threading
import time

EXPECTED_NODE_VERSION = "v24.13.1"
EXPECTED_ACPX_VERSION = "0.12.0"
FAKE_KEY = "AVA_ACPX_FAKE_KEY_NOT_A_SECRET"
MAX_STDOUT = 1024 * 1024
MAX_STDERR = 16 * 1024
ACTIVE_PROCESSES = []
ACTIVE_ROOTS = set()


class BoundedReader:
    def __init__(self, stream, limit):
        self.stream = stream
        self.limit = limit
        self.buffer = bytearray()
        self.truncated = False
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self):
        while True:
            chunk = self.stream.read(8192)
            if not chunk:
                return
            remaining = self.limit - len(self.buffer)
            if remaining > 0:
                self.buffer.extend(chunk[:remaining])
            if len(chunk) > remaining:
                self.truncated = True

    def finish(self):
        self.thread.join(timeout=2)
        if self.thread.is_alive():
            raise RuntimeError("bounded process-output reader did not finish")
        suffix = b"\n<output truncated>" if self.truncated else b""
        return bytes(self.buffer) + suffix


class ManagedProcess:
    def __init__(self, argv, *, env, cwd, stdout_limit=MAX_STDERR, stderr_limit=MAX_STDERR):
        self.process = subprocess.Popen(
            argv,
            cwd=cwd,
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            start_new_session=True,
        )
        self.pgid = self.process.pid
        self.stdout_reader = BoundedReader(self.process.stdout, stdout_limit)
        self.stderr_reader = BoundedReader(self.process.stderr, stderr_limit)
        self.stdout = b""
        self.stderr = b""
        ACTIVE_PROCESSES.append(self)

    def wait(self, timeout):
        code = self.process.wait(timeout=timeout)
        self.stdout = self.stdout_reader.finish()
        self.stderr = self.stderr_reader.finish()
        return code

    def group_alive(self):
        try:
            os.killpg(self.pgid, 0)
            return True
        except ProcessLookupError:
            return False

    def wait_group(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and self.group_alive():
            self.process.poll()  # Reap an exited group leader while checking descendants.
            time.sleep(0.02)
        return not self.group_alive()

    def terminate(self):
        if self.group_alive():
            try:
                os.killpg(self.pgid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            if not self.wait_group(0.75):
                try:
                    os.killpg(self.pgid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                if not self.wait_group(1.5):
                    raise RuntimeError(f"owned process group {self.pgid} survived SIGKILL")
        if self.process.poll() is None:
            self.process.wait(timeout=1.5)
        if not self.stdout:
            self.stdout = self.stdout_reader.finish()
        if not self.stderr:
            self.stderr = self.stderr_reader.finish()

    def assert_group_gone(self):
        if not self.wait_group(1):
            raise AssertionError(f"owned process group {self.pgid} survived cleanup")


def discard_process(managed):
    if managed in ACTIVE_PROCESSES:
        ACTIVE_PROCESSES.remove(managed)


def handle_signal(signum, _frame):
    for managed in list(ACTIVE_PROCESSES):
        try:
            managed.terminate()
        except Exception:
            try:
                os.killpg(managed.pgid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
    for root in list(ACTIVE_ROOTS):
        shutil.rmtree(root, ignore_errors=True)
    os._exit(128 + signum)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ava", required=True)
    parser.add_argument("--fake-provider", required=True)
    parser.add_argument("--node", required=True)
    parser.add_argument("--acpx", required=True)
    parser.add_argument("--root")
    return parser.parse_args()


def run_preflight(argv, env, cwd):
    managed = ManagedProcess(argv, cwd=cwd, env=env)
    try:
        code = managed.wait(5)
        assert code == 0, (argv, code, managed.stderr.decode(errors="replace"))
        assert managed.stderr == b"", (argv, managed.stderr.decode(errors="replace"))
        return managed.stdout.decode("utf-8", errors="strict").strip()
    finally:
        managed.terminate()
        managed.assert_group_gone()
        discard_process(managed)


def clean_environment(root, node, provider_port=None):
    guard_bin = root / "guard-bin"
    node_bin = Path(node).absolute().parent
    env = {
        "HOME": str(root / "home"),
        "XDG_CONFIG_HOME": str(root / "xdg-config"),
        "XDG_DATA_HOME": str(root / "xdg-data"),
        "XDG_STATE_HOME": str(root / "xdg-state"),
        "XDG_CACHE_HOME": str(root / "xdg-cache"),
        "TMPDIR": str(root / "tmp"),
        "PATH": f"{node_bin}:{guard_bin}:/usr/bin:/bin",
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
        "NO_COLOR": "1",
        "AVA_ACP_STDERR": str(root / "ava.stderr"),
        "AVA_ACP_WRAPPER_PROOF": str(root / "ava-wrapper.proof"),
    }
    if provider_port is not None:
        env.update({
            "MOONSHOT_BASE_URL": f"http://127.0.0.1:{provider_port}",
            "MOONSHOT_API_KEY": FAKE_KEY,
        })
    return env


def prepare_roots(base):
    names = [
        "home/.acpx", "xdg-config", "xdg-data", "xdg-state", "xdg-cache",
        "tmp", "workspace", "workspace/src", "provider", "guard-bin",
    ]
    for name in names:
        (base / name).mkdir(parents=True, exist_ok=True)


def configure_model(root):
    config = root / "xdg-config/ava"
    config.mkdir(parents=True, exist_ok=True)
    model = {
        "default_provider": "moonshot",
        "default_model": "acpx-interop",
        "models": [{
            "provider": "moonshot",
            "id": "acpx-interop",
            "name": "acpx Interop",
            "family": "fake",
            "context_window_tokens": 8192,
            "max_output_tokens": 1024,
            "supports_tools": True,
            "supports_streaming": False,
            "input_modalities": ["text"],
            "output_modalities": ["text"],
        }],
    }
    (config / "models.json").write_text(json.dumps(model) + "\n", encoding="utf-8")


def make_download_guards(root):
    marker = root / "adapter-download-attempted"
    script = f"#!/bin/sh\nprintf '%s\\n' \"$0 $*\" >> {shlex.quote(str(marker))}\nexit 97\n"
    for command in ("npm", "npx", "git", "ssh"):
        path = root / "guard-bin" / command
        path.write_text(script, encoding="utf-8")
        path.chmod(0o700)
    return marker


def make_ava_wrapper(root, ava):
    wrapper = root / "ava-acp-wrapper"
    script = """#!/bin/sh
set -eu
if [ "$#" -ne 0 ]; then
  echo "unexpected AVA wrapper arguments" >&2
  exit 64
fi
printf 'exec ava --acp\\n' > "$AVA_ACP_WRAPPER_PROOF"
exec %s --acp 2>"$AVA_ACP_STDERR"
""" % shlex.quote(str(Path(ava).absolute()))
    wrapper.write_text(script, encoding="utf-8")
    wrapper.chmod(0o700)
    return wrapper


def wait_for_port(port_file, provider):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if port_file.exists():
            value = int(port_file.read_text(encoding="ascii").strip())
            assert 0 < value <= 65535
            return value
        if provider.process.poll() is not None:
            provider.wait(1)
            raise AssertionError(
                f"fake provider exited during startup: {provider.process.returncode}: "
                f"{provider.stderr.decode(errors='replace')}"
            )
        time.sleep(0.02)
    raise AssertionError("fake provider did not publish a loopback port")


def assert_raw_records(records):
    assert records, "acpx emitted no JSON records"
    assert all(isinstance(record, dict) and record.get("jsonrpc") == "2.0" for record in records)
    methods = [record.get("method") for record in records]
    for method in ("initialize", "session/new", "session/prompt", "session/update"):
        assert method in methods, (method, methods)
    initialize = next(record for record in records if record.get("method") == "initialize")
    assert initialize["params"]["protocolVersion"] == 1
    assert any(record.get("id") == initialize.get("id") and record.get("result", {}).get("protocolVersion") == 1 for record in records)
    prompt = next(record for record in records if record.get("method") == "session/prompt")
    assert any(
        record.get("id") == prompt.get("id") and record.get("result", {}).get("stopReason") == "end_turn"
        for record in records
    ), "acpx did not expose the raw end-turn prompt response"
    assert any(
        record.get("method") == "session/update"
        and record.get("params", {}).get("update", {}).get("sessionUpdate") in {"tool_call", "tool_call_update", "agent_message_chunk"}
        for record in records
    ), "acpx did not expose a raw session/update"


def main():
    args = parse_args()
    os.environ.clear()
    if os.name != "posix":
        raise AssertionError("owned process-group acpx harness currently requires POSIX")
    parent = Path(args.root).absolute() if args.root else Path(tempfile.gettempdir())
    parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="ava-acpx-", dir=parent))
    ACTIVE_ROOTS.add(root)
    provider = None
    acpx = None
    try:
        prepare_roots(root)
        configure_model(root)
        download_marker = make_download_guards(root)
        wrapper = make_ava_wrapper(root, args.ava)
        base_env = clean_environment(root, args.node)

        node_version = run_preflight([str(Path(args.node).absolute()), "--version"], base_env, root)
        assert node_version == EXPECTED_NODE_VERSION, (node_version, EXPECTED_NODE_VERSION)
        acpx_version = run_preflight([str(Path(args.acpx).absolute()), "--version"], base_env, root)
        assert acpx_version == EXPECTED_ACPX_VERSION, (acpx_version, EXPECTED_ACPX_VERSION)

        provider_root = root / "provider"
        port_file = provider_root / "port"
        request_log = provider_root / "requests.log"
        effect = root / "workspace" / "acpx-effect.txt"
        provider = ManagedProcess(
            [str(Path(args.fake_provider).absolute()), str(port_file), str(request_log), "0", "write-tool", str(effect)],
            env=base_env,
            cwd=root / "workspace",
        )
        port = wait_for_port(port_file, provider)
        env = clean_environment(root, args.node, port)
        command = [
            str(Path(args.acpx).absolute()),
            "--agent", str(wrapper),
            "--cwd", str(root / "workspace"),
            "--format", "json",
            "--json-strict",
            "--approve-all",
            "--non-interactive-permissions", "fail",
            "--auth-policy", "fail",
            "--timeout", "15",
            "exec",
            "create the requested deterministic workspace effect",
        ]
        acpx = ManagedProcess(command, env=env, cwd=root / "workspace", stdout_limit=MAX_STDOUT)
        assert acpx.wait(25) == 0, acpx.stderr.decode(errors="replace")
        assert not acpx.stdout_reader.truncated, "acpx JSON output exceeded its bound"
        assert acpx.stderr == b"", acpx.stderr.decode(errors="replace")
        records = [json.loads(line) for line in acpx.stdout.decode("utf-8").splitlines() if line.strip()]
        assert_raw_records(records)

        assert effect.read_text(encoding="utf-8") == "rpc new\n"
        provider_log = request_log.read_text(encoding="utf-8")
        assert "rpc new" in provider_log, "provider did not observe the client-owned tool effect"
        assert provider.wait(7) == 0, provider.stderr.decode(errors="replace")
        assert provider.stdout == b"" and provider.stderr == b""
        assert (root / "ava.stderr").read_bytes() == b"", "successful AVA stderr was not clean"
        assert (root / "ava-wrapper.proof").read_text(encoding="utf-8") == "exec ava --acp\n"
        assert not download_marker.exists(), "acpx attempted an adapter/package download"
        assert not any((root / "home/.acpx").rglob("node_modules")), "acpx installed an adapter into isolated state"

        acpx.assert_group_gone()
        provider.assert_group_gone()
        print("opt-in acpx 0.12.0 raw-agent interoperability smoke passed")
        return 0
    except subprocess.TimeoutExpired as error:
        print(f"acpx interoperability timeout: {error}", file=sys.stderr)
        return 1
    except Exception as error:
        message = str(error).replace(str(root), "<interop-root>").replace(FAKE_KEY, "<fake-key>")[:MAX_STDERR]
        print(f"acpx interoperability check failed: {message}", file=sys.stderr)
        return 1
    finally:
        if acpx is not None:
            acpx.terminate()
            acpx.assert_group_gone()
            discard_process(acpx)
        if provider is not None:
            provider.terminate()
            provider.assert_group_gone()
            discard_process(provider)
        shutil.rmtree(root, ignore_errors=True)
        ACTIVE_ROOTS.discard(root)


if __name__ == "__main__":
    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)
    raise SystemExit(main())
