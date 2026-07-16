#!/usr/bin/env python3
"""Real-process RPC parser depth/duplicate-key recovery regressions."""

import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ava", required=True)
    parser.add_argument("--root", required=True)
    args = parser.parse_args()

    root = Path(args.root)
    root.mkdir(parents=True, exist_ok=True)
    libcwd_rcfile = (root / "libcwdrc").resolve()
    libcwd_rcfile.write_text(
        "silent = on\nchannels_default = off\n", encoding="utf-8")
    env = os.environ.copy()
    env.update({
        "HOME": str(root / "home"),
        "XDG_CONFIG_HOME": str(root / "config"),
        "XDG_DATA_HOME": str(root / "data"),
        "XDG_STATE_HOME": str(root / "state"),
        "XDG_CACHE_HOME": str(root / "cache"),
        "NO_COLOR": "1",
        # Debug builds must remain protocol-quiet even when an isolated HOME
        # has no developer libcwd configuration.
        "LIBCWD_RCFILE_NAME": str(libcwd_rcfile),
    })
    process = subprocess.Popen(
        [args.ava, "--rpc", "--no-session", "--offline"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
    )

    depth = 100_000
    deep = b'{"id":"deep","type":"get_protocol","future":' + (b'[' * depth) + (b']' * depth) + b'}\n'
    duplicate_records = [
        b'{"id":"first","id":"second","type":"get_protocol"}\n',
        b'{"id":"type","type":"get_protocol","t\\u0079pe":"get_state"}\n',
        b'{"id":"method","type":"get_protocol","method":1,"method":2}\n',
        b'{"id":"jsonrpc","type":"get_protocol","jsonrpc":"2.0","jsonrpc":"1.0"}\n',
        b'{"id":"nested","type":"get_protocol","future":{"unique":1,"unique":2}}\n',
    ]
    process.stdin.write(deep)
    for record in duplicate_records:
        process.stdin.write(record)
    process.stdin.write(b'{"id":"usable","type":"get_protocol","future":{"unique":true}}\n')
    process.stdin.close()

    responses = [json.loads(process.stdout.readline()) for _ in range(7)]
    assert responses[0]["id"] == "deep"
    assert responses[0]["error"]["code"] == "invalid_request"
    assert "nesting" in responses[0]["error"]["message"]
    for response in responses[1:6]:
        assert response["success"] is False
        assert response["error"]["code"] == "invalid_request"
        assert "duplicate" in response["error"]["message"]
    assert responses[6]["id"] == "usable" and responses[6]["success"] is True
    assert process.wait(timeout=5) == 0
    assert process.stdout.read() == b""
    assert process.stderr.read() == b""
    print("real RPC strict-parser recovery checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
