"""
ava_plugin.sdk -- SDK for building AVA plugins in Python.

Plugins communicate with AVA via JSON-RPC 2.0 over stdio using
Content-Length framing (identical to LSP/MCP wire format).

Usage:
    from ava_plugin import create_plugin

    def on_session_start(ctx, params):
        return {}

    create_plugin({
        "session.start": on_session_start,
    })
"""

import json
import sys

# All valid hook names that AVA supports.
VALID_HOOKS = frozenset([
    "auth",
    "auth.refresh",
    "request.headers",
    "tool.before",
    "tool.after",
    "agent.before",
    "agent.after",
    "session.start",
    "session.end",
    "config",
    "event",
    "shell.env",
])


class PluginContext:
    """Stores project context received during initialization."""

    __slots__ = ("project", "config", "tools")

    def __init__(self):
        self.project = {"directory": "", "name": ""}
        self.config = {}
        self.tools = []


def _write_message(msg):
    """Write a JSON-RPC message to stdout with Content-Length framing."""
    payload = json.dumps(msg, separators=(",", ":"))
    payload_bytes = payload.encode("utf-8")
    header = "Content-Length: %d\r\n\r\n" % len(payload_bytes)
    out = sys.stdout.buffer
    out.write(header.encode("utf-8"))
    out.write(payload_bytes)
    out.flush()


def _send_result(msg_id, result):
    """Send a successful JSON-RPC response."""
    _write_message({"jsonrpc": "2.0", "id": msg_id, "result": result})


def _send_error(msg_id, code, message, data=None):
    """Send a JSON-RPC error response."""
    err = {"code": code, "message": message}
    if data is not None:
        err["data"] = data
    _write_message({"jsonrpc": "2.0", "id": msg_id, "error": err})


def _read_messages(stream):
    """Generator that yields parsed JSON-RPC messages from a byte stream
    using Content-Length framing.

    Reads headers byte-by-byte until \\r\\n\\r\\n, then reads exactly
    Content-Length bytes for the body. This avoids blocking on buffered reads.
    """
    while True:
        # Read headers byte-by-byte until we see \r\n\r\n
        header_bytes = b""
        while True:
            b = stream.read(1)
            if not b:
                return  # EOF
            header_bytes += b
            if header_bytes.endswith(b"\r\n\r\n"):
                break

        # Parse Content-Length from headers
        header_text = header_bytes[:-4].decode("utf-8", errors="replace")
        content_length = None
        for line in header_text.split("\r\n"):
            if ":" in line:
                key, value = line.split(":", 1)
                if key.strip().lower() == "content-length":
                    content_length = int(value.strip())
                    break
        if content_length is None:
            continue  # Skip malformed header block

        # Read exactly content_length bytes for the body
        body_bytes = b""
        remaining = content_length
        while remaining > 0:
            chunk = stream.read(remaining)
            if not chunk:
                return  # EOF
            body_bytes += chunk
            remaining -= len(chunk)

        try:
            yield json.loads(body_bytes.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            pass  # Ignore unparseable messages


def create_plugin(hooks):
    """Create and start an AVA plugin.

    Reads JSON-RPC messages from stdin, dispatches to user-defined hook
    handlers, and writes responses to stdout.

    Args:
        hooks: Dict mapping hook names (e.g. "session.start", "tool.before")
               to handler callables. Each handler receives (ctx, params) and
               should return a dict or None. Raise an Exception to send a
               JSON-RPC error response.
    """
    ctx = PluginContext()

    for msg in _read_messages(sys.stdin.buffer):
        method = msg.get("method")
        msg_id = msg.get("id")
        params = msg.get("params") or {}

        # -- initialize --
        if method == "initialize":
            project = params.get("project")
            if isinstance(project, dict):
                ctx.project = {
                    "directory": project.get("directory", ""),
                    "name": project.get("name", ""),
                }
            config = params.get("config")
            if isinstance(config, dict):
                ctx.config = config
            tools = params.get("tools")
            if isinstance(tools, list):
                ctx.tools = tools
            _send_result(msg_id, {"hooks": list(hooks.keys())})
            continue

        # -- shutdown --
        if method == "shutdown":
            sys.exit(0)

        # -- hook/* dispatch --
        if method and method.startswith("hook/"):
            hook_name = method[5:]
            handler = hooks.get(hook_name)
            if handler is None:
                if msg_id is not None:
                    _send_error(msg_id, -32601, "no handler for hook '%s'" % hook_name)
                continue
            try:
                result = handler(ctx, params)
                if msg_id is not None:
                    _send_result(msg_id, result if result is not None else None)
            except Exception as exc:
                if msg_id is not None:
                    _send_error(msg_id, -32000, str(exc))
                else:
                    sys.stderr.write("[plugin] hook error: %s\n" % exc)
            continue

        # -- unknown method --
        if msg_id is not None:
            _send_error(msg_id, -32601, "unknown method '%s'" % method)
