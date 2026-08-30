"""Cross-process one-shot gates multiplexed over one connected Unix socket."""

from __future__ import annotations

import select
import socket
import time
from dataclasses import dataclass


PROCESS_GATE_FD_ENV = "AVA_TEST_CONTROL_FD"
_GATE_COUNT = 64


def _validate_gate(gate: int) -> None:
    """Reject values outside the one-byte process-gate protocol range."""

    if isinstance(gate, bool) or not isinstance(gate, int) or not 0 <= gate < _GATE_COUNT:
        raise ValueError(f"process gate must be an integer in [0, {_GATE_COUNT - 1}]")


class ProcessGateSet:
    """Open and wait for up to 64 persistent gates over one owned socket.

    Messages consist of one gate-number byte. Received opens are retained in a
    bit mask, so open and wait order do not need to match. Calls on one endpoint
    must be serialized by its owner; ``close`` interrupts future operations
    with an ordinary socket error or EOF diagnostic.
    """

    def __init__(self, control_socket: socket.socket) -> None:
        self._socket = control_socket
        self._opened_by_peer = 0
        self._opened_for_peer = 0

    @property
    def fileno(self) -> int:
        """Return the connected descriptor while this endpoint remains open."""

        return self._socket.fileno()

    def open(self, gate: int) -> None:
        """Permanently open ``gate`` for the peer, sending at most one byte."""

        _validate_gate(gate)
        bit = 1 << gate
        if self._opened_for_peer & bit:
            return
        try:
            self._socket.sendall(bytes((gate,)))
        except OSError as exc:
            raise RuntimeError(f"failed to open process gate {gate}: peer control socket failed") from exc
        self._opened_for_peer |= bit

    def wait(self, gate: int, timeout: float) -> None:
        """Wait up to ``timeout`` seconds for the peer to open ``gate``.

        Opens for other gates are cached while waiting. If the peer already
        sent the requested open, buffered socket data is consumed immediately;
        peer exit before the open is reported as an error rather than success.
        """

        _validate_gate(gate)
        if timeout < 0:
            raise ValueError("process gate timeout must be nonnegative")
        bit = 1 << gate
        deadline = time.monotonic() + timeout
        while not self._opened_by_peer & bit:
            remaining = max(0.0, deadline - time.monotonic())
            try:
                readable, _, _ = select.select((self._socket,), (), (), remaining)
            except OSError as exc:
                raise RuntimeError(f"failed waiting for process gate {gate}: control socket poll failed") from exc
            if not readable:
                raise TimeoutError(f"timed out waiting for process gate {gate}")
            try:
                received = self._socket.recv(_GATE_COUNT)
            except OSError as exc:
                raise RuntimeError(f"failed waiting for process gate {gate}: control socket read failed") from exc
            if not received:
                raise RuntimeError(f"peer exited before opening process gate {gate}")
            for opened_gate in received:
                if opened_gate >= _GATE_COUNT:
                    raise RuntimeError(f"peer sent invalid process gate {opened_gate}")
                self._opened_by_peer |= 1 << opened_gate

    def close(self) -> None:
        """Close this endpoint; repeated closes are harmless."""

        self._socket.close()


@dataclass
class ProcessGatePair:
    """Hold the harness endpoint and child endpoint until process launch.

    The child socket must remain open through ``Popen`` and be listed in
    ``pass_fds``. After a successful launch, ``close_child_endpoint`` leaves the
    child process as the sole owner, so premature child exit produces EOF.
    """

    gates: ProcessGateSet
    child_socket: socket.socket

    @property
    def child_fd(self) -> int:
        """Return the descriptor number to place in the child environment."""

        return self.child_socket.fileno()

    def close_child_endpoint(self) -> None:
        """Drop the harness's duplicate of the endpoint inherited by the child."""

        self.child_socket.close()

    def close(self) -> None:
        """Close both endpoints, including partially launched setup state."""

        self.child_socket.close()
        self.gates.close()


def create_process_gate_pair() -> ProcessGatePair:
    """Create a private socket pair for one harness and one directly launched child."""

    harness_socket, child_socket = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
    return ProcessGatePair(ProcessGateSet(harness_socket), child_socket)
