#!/usr/bin/env python3
"""
Example AVA plugin that logs session events.

Run standalone:
    python3 examples/hello_plugin.py

Or install as an AVA plugin:
    ava plugin add plugins/examples/hello-python
"""

import sys
import os

# Allow running from the examples/ directory or the sdk-python/ directory.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from ava_plugin import create_plugin


def on_session_start(ctx, params):
    goal = params.get("goal", "unknown")
    sys.stderr.write("[hello] Session started — goal: %s\n" % goal)
    sys.stderr.write("[hello] Project: %s (%s)\n" % (ctx.project["name"], ctx.project["directory"]))
    return {}


def on_session_end(ctx, params):
    sys.stderr.write("[hello] Session ended\n")
    return {}


def on_tool_before(ctx, params):
    tool = params.get("tool", "")
    sys.stderr.write("[hello] Tool called: %s\n" % tool)
    return {"args": params.get("args", {})}


def on_tool_after(ctx, params):
    tool = params.get("tool", "")
    sys.stderr.write("[hello] Tool finished: %s\n" % tool)
    return {}


create_plugin({
    "session.start": on_session_start,
    "session.end": on_session_end,
    "tool.before": on_tool_before,
    "tool.after": on_tool_after,
})
