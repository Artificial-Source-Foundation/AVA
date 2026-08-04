"""Credential-free real-terminal coverage for the path-free startup overview."""

from __future__ import annotations

import re

from tui_smoke_helpers import (
    ACTIVE_CONTEXT_STATUS_PATTERN,
    SmokeContext,
    capture,
    save_evidence,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_absent,
    wait_for_screen_change,
)
from .common import _finish_main, _main_session


def scenario_startup_overview(ctx: SmokeContext) -> None:
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)
    # Main startup does not start a fake provider; prove no request log is created either.
    request_logs = sorted(ctx.root.glob("*-provider-requests.log"))
    session_dir = ctx.state / "ava" / "sessions"

    def session_jsonl_snapshot() -> dict[str, str]:
        files: dict[str, str] = {}
        if session_dir.exists():
            for path in sorted(session_dir.rglob("*.jsonl")):
                files[str(path.relative_to(session_dir))] = path.read_text(encoding="utf-8", errors="replace")
        return files

    before_jsonl = session_jsonl_snapshot()
    before_request_bytes = sum(path.stat().st_size for path in request_logs if path.exists())

    initial = wait_for(tmux_exe, session, r"Type a message|/overview", "startup overview initial frame")
    if "/overview" not in initial:
        raise RuntimeError(f"collapsed startup overview did not expose /overview\nscreen:\n{initial}")
    if re.search(r"/home/|AGENTS\.md|api[_-]?key|OPENAI_API_KEY|auth\.json", initial, re.IGNORECASE):
        raise RuntimeError(f"collapsed overview leaked private path/secret text\nscreen:\n{initial}")
    footer_lines = [line for line in initial.splitlines() if "GPT-5.5" in line and "ctx " in line]
    if not footer_lines:
        raise RuntimeError(f"composer footer missing from startup overview frame\nscreen:\n{initial}")
    footer_text = footer_lines[-1].removeprefix("│  ").strip()
    if not re.fullmatch(rf"GPT-5\.5 · ctx {ACTIVE_CONTEXT_STATUS_PATTERN}", footer_text):
        raise RuntimeError(f"overview chrome changed the quiet composer footer\nscreen:\n{initial}")
    save_evidence(root, "startup-overview-collapsed", initial)

    # Expand via exact /overview (selector bypass).
    send_literal(tmux_exe, session, "/overview")
    send_keys(tmux_exe, session, "Enter")
    expanded = wait_for(tmux_exe, session, r"Startup overview|Mode|Provider", "overview expanded select-list")
    if "Startup overview" not in expanded:
        raise RuntimeError(f"/overview did not open the read-only select-list\nscreen:\n{expanded}")
    if re.search(r"MCP\s*/\s*LSP|enabled MCP|LSP enabled", expanded, re.IGNORECASE):
        raise RuntimeError(f"expanded overview made MCP/LSP enablement claims\nscreen:\n{expanded}")
    save_evidence(root, "startup-overview-expanded", expanded)

    # Filter + scroll remain host-owned list behavior.
    # Ctrl+U is composer-only; select-list filters clear with Backspace.
    send_literal(tmux_exe, session, "mode")
    filtered = wait_for(tmux_exe, session, r"filter\s+mode", "overview filter mode")
    if "Mode" not in filtered:
        raise RuntimeError(f"overview filter did not keep the Mode row\nscreen:\n{filtered}")
    for _ in range(4):
        send_keys(tmux_exe, session, "BSpace")
    wait_for_absent(tmux_exe, session, r"filter\s+mode", "overview filter cleared")
    send_keys(tmux_exe, session, "PageDown")
    paged = capture(tmux_exe, session)
    save_evidence(root, "startup-overview-paged", paged)

    # Esc closes without backend mutation.
    before_close = capture(tmux_exe, session)
    send_keys(tmux_exe, session, "Escape")
    closed = wait_for_absent(tmux_exe, session, r"Startup overview", "overview closed via Esc")
    if "/overview" not in closed and "Type a message" not in closed:
        raise RuntimeError(f"closing overview did not restore composer chrome\nscreen:\n{closed}")
    save_evidence(root, "startup-overview-closed", closed)

    # Short + narrow resize keeps expanded view usable.
    send_literal(tmux_exe, session, "/overview")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Startup overview", "overview reopened before resize")
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "48", "-y", "10")
    dimensions = tmux(tmux_exe, "display-message", "-p", "-t", session, "#{window_width},#{window_height}").stdout.strip()
    if dimensions != "48,10":
        raise RuntimeError(f"short/narrow resize dimensions were {dimensions}, expected 48,10")
    short = wait_for(tmux_exe, session, r"Startup overview|Mode|filter", "overview usable at 48x10")
    if "Startup overview" not in short:
        raise RuntimeError(f"overview select-list did not remain usable at short height\nscreen:\n{short}")
    save_evidence(root, "startup-overview-short-narrow", short)

    # Restore ordinary geometry and close.
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "100", "-y", "24")
    wait_for(tmux_exe, session, r"Startup overview", "overview restored after resize")
    send_keys(tmux_exe, session, "Escape")
    restored = wait_for_absent(tmux_exe, session, r"Startup overview", "overview closed after resize")

    # Mouse open from collapsed card (first content row).
    collapsed = wait_for(tmux_exe, session, r"/overview", "collapsed overview before mouse open")
    overview_row = next(
        ((index + 1, line) for index, line in enumerate(collapsed.splitlines()) if "/overview" in line),
        None,
    )
    if overview_row is None:
        raise RuntimeError(f"collapsed overview row not found for mouse hit\nscreen:\n{collapsed}")
    row_number, row_text = overview_row
    # Click near the start of the overview card content.
    column = max(2, min(len(row_text.rstrip()) // 2, 20))
    before_mouse = capture(tmux_exe, session)
    send_literal(tmux_exe, session, f"\x1b[<0;{column};{row_number}M")
    send_literal(tmux_exe, session, f"\x1b[<0;{column};{row_number}m")
    mouse_opened = wait_for_screen_change(tmux_exe, session, before_mouse, "overview mouse open")
    if "Startup overview" not in mouse_opened:
        # Some terminals coalesce press/release; retry with click-only report.
        send_literal(tmux_exe, session, f"\x1b[<0;{column};{row_number}M")
        mouse_opened = wait_for(tmux_exe, session, r"Startup overview", "overview mouse open retry")
    save_evidence(root, "startup-overview-mouse-open", mouse_opened)
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Startup overview", "overview closed after mouse open")

    after_jsonl = session_jsonl_snapshot()
    if after_jsonl != before_jsonl:
        raise RuntimeError(
            "startup overview mutated session JSONL\n"
            f"before keys={sorted(before_jsonl)}\nafter keys={sorted(after_jsonl)}"
        )
    after_logs = sorted(ctx.root.glob("*-provider-requests.log"))
    after_request_bytes = sum(path.stat().st_size for path in after_logs if path.exists())
    if after_logs or after_request_bytes != before_request_bytes:
        raise RuntimeError(
            "startup overview issued provider requests\n"
            f"before bytes={before_request_bytes} after bytes={after_request_bytes} logs={after_logs}"
        )

    # Keep restored capture referenced so short-height cleanup stays intentional.
    if "Type a message" not in restored and "/overview" not in restored:
        raise RuntimeError(f"final restored frame lost composer chrome\nscreen:\n{restored}")

    _finish_main(tmux_exe, session)
