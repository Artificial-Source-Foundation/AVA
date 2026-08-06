"""Runtime-aware timeout helpers shared by Python test drivers."""

from __future__ import annotations

import os


DEFAULT_DEBUG_TIMEOUT_SECONDS = 3600.0


def test_timeout(authored_seconds: float) -> float:
    """Return the authored timeout unless debug timeout stretching is enabled.

    AVA_DEBUG_NO_TIMEOUT stretches the timeout to one hour. A positive integral
    AVA_DEBUG_NO_TIMEOUT_SECONDS overrides that default; missing or invalid
    overrides deliberately retain the one-hour fallback used by CTest and the
    C++ test timeout helper.
    """

    if "AVA_DEBUG_NO_TIMEOUT" not in os.environ:
        return authored_seconds

    override = os.environ.get("AVA_DEBUG_NO_TIMEOUT_SECONDS", "")
    if not override.isascii() or not override.isdigit():
        return DEFAULT_DEBUG_TIMEOUT_SECONDS
    try:
        seconds = int(override, 10)
    except ValueError:
        return DEFAULT_DEBUG_TIMEOUT_SECONDS
    return float(seconds) if seconds > 0 else DEFAULT_DEBUG_TIMEOUT_SECONDS
