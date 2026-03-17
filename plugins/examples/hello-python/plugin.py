#!/usr/bin/env python3
"""
Hello Python — example AVA plugin using the Python SDK.

Install:
    ava plugin add plugins/examples/hello-python

Remove:
    ava plugin remove hello-python
"""

import sys
import os

# Add the SDK to the path so we can import it without pip install.
sdk_dir = os.path.join(os.path.dirname(__file__), "..", "..", "sdk-python")
sys.path.insert(0, sdk_dir)

from ava_plugin import create_plugin


def on_session_start(ctx, params):
    goal = params.get("goal", "unknown")
    sys.stderr.write("[hello-python] Session started — goal: %s\n" % goal)
    sys.stderr.write("[hello-python] Project: %s at %s\n" % (
        ctx.project["name"], ctx.project["directory"]
    ))
    sys.stderr.write("[hello-python] Available tools: %s\n" % ", ".join(ctx.tools))
    return {}


def on_session_end(ctx, params):
    sys.stderr.write("[hello-python] Session ended\n")
    return {}


def on_tool_before(ctx, params):
    tool = params.get("tool", "")
    call_id = params.get("call_id", "?")
    sys.stderr.write("[hello-python] -> %s(%s)\n" % (tool, call_id))
    return {"args": params.get("args", {})}


def on_tool_after(ctx, params):
    tool = params.get("tool", "")
    call_id = params.get("call_id", "?")
    sys.stderr.write("[hello-python] <- %s(%s)\n" % (tool, call_id))
    return {}


create_plugin({
    "session.start": on_session_start,
    "session.end": on_session_end,
    "tool.before": on_tool_before,
    "tool.after": on_tool_after,
})
