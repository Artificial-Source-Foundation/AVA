#!/usr/bin/env python3
"""Cross-language regression tests for the numbered process-gate protocol."""

from __future__ import annotations

import argparse
import os
import subprocess

from process_gate import PROCESS_GATE_FD_ENV, create_process_gate_pair


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--peer", required=True)
    args = parser.parse_args()

    # Before launch, retaining the child endpoint makes an unopened wait time
    # out rather than report EOF. The same property holds while the child owns
    # that endpoint before it has completed exec or initialization.
    prelaunch = create_process_gate_pair()
    try:
        for invalid_gate in (-1, 64, True):
            try:
                prelaunch.gates.open(invalid_gate)
                raise RuntimeError(f"invalid process gate {invalid_gate!r} was accepted")
            except ValueError:
                pass
        try:
            prelaunch.gates.wait(0, 0.01)
            raise RuntimeError("unopened prelaunch gate unexpectedly opened")
        except TimeoutError:
            pass
    finally:
        prelaunch.close()

    pair = create_process_gate_pair()
    environment = os.environ.copy()
    environment[PROCESS_GATE_FD_ENV] = str(pair.child_fd)
    try:
        process = subprocess.Popen(
            [args.peer],
            env=environment,
            pass_fds=(pair.child_fd,),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except BaseException:
        pair.close()
        raise
    pair.close_child_endpoint()

    try:
        pair.gates.wait(2, 2.0)
        pair.gates.wait(7, 0.0)
        pair.gates.open(1)
        pair.gates.open(5)
        pair.gates.open(1)
        pair.gates.wait(3, 2.0)
        pair.gates.wait(12, 2.0)
        pair.gates.wait(63, 2.0)
        pair.gates.wait(63, 0.0)
        stdout, stderr = process.communicate(timeout=2.0)
        require(process.returncode == 0, f"process-gate peer failed ({process.returncode})\nstdout:\n{stdout}\nstderr:\n{stderr}")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=2.0)
        pair.gates.close()

    exited = create_process_gate_pair()
    exited.close_child_endpoint()
    try:
        try:
            exited.gates.wait(4, 0.1)
            raise RuntimeError("peer EOF unexpectedly opened a process gate")
        except RuntimeError as error:
            require("peer exited" in str(error), f"peer EOF produced the wrong diagnostic: {error}")
    finally:
        exited.gates.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
