"""The tmux TUI smoke scenario for main permission flow."""

from __future__ import annotations

import re

from tui_smoke_helpers import (
    ACTIVE_CONTEXT_STATUS_PATTERN,
    SmokeContext,
    capture,
    capture_styled,
    save_evidence,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_absent,
    wait_for_screen_change,
)
from .common import _finish_main, _main_session


def scenario_main_permission_flow(ctx: SmokeContext) -> None:
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)

    def assert_f5_frame(screen: str, width: int, height: int, label: str) -> None:
        dimensions = tmux(
            tmux_exe, "display-message", "-p", "-t", session, "#{window_width},#{window_height}"
        ).stdout.strip()
        lines = screen.splitlines()
        if dimensions != f"{width},{height}" or len(lines) != height or any(len(line) > width for line in lines):
            raise RuntimeError(f"{label} did not retain an exact {width}x{height} terminal frame\nscreen:\n{screen}")
        if "\x1b" in screen or any(ord(character) < 32 and character != "\n" for character in screen):
            raise RuntimeError(f"{label} contained terminal control bytes\nscreen:\n{screen}")
        if "[" in screen or "]" in screen or "---" in screen:
            raise RuntimeError(f"{label} retained obsolete dashed or bracketed UI chrome\nscreen:\n{screen}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/bash git push origin main")
    send_keys(tmux_exe, session, "Enter")
    permission = wait_for(
        tmux_exe,
        session,
        r"(?s)! Permission required.*Shell command.*\$ git push origin main.*risk critical.*reason sealed.*› Reject.*Allow once",
        "permission prompt risk metadata",
    )
    assert_f5_frame(permission, 120, 32, "roomy permission prompt")
    if (
        "permreq_" in permission
        or "Always allow" in permission
        or ("Always reject" not in permission and "Never" not in permission)
    ):
        raise RuntimeError(f"critical raw-shell prompt did not expose only truthful one-shot/deny choices\nscreen:\n{permission}")
    save_evidence(root, "frontend-f5-permission-prompt-roomy", permission)
    save_evidence(root, "permission-prompt-risk-reason", permission)
    wheel_down = "\x1b[<65;4;6M"
    wheel_up = "\x1b[<64;4;6M"
    send_literal(tmux_exe, session, wheel_down * 12)
    permission_burst = wait_for(
        tmux_exe,
        session,
        r"(?s)! Permission required.*Reject.*› Allow once",
        "permission same-direction wheel burst governed to one choice",
    )
    if "› Allow once" not in permission_burst:
        raise RuntimeError(f"permission wheel burst did not settle on Allow once\nscreen:\n{permission_burst}")
    send_literal(tmux_exe, session, "x")
    send_literal(tmux_exe, session, wheel_up)
    deliberate_wheel = wait_for(
        tmux_exe,
        session,
        r"(?s)! Permission required.*› Reject.*Allow once",
        "permission wheel accepted after the non-wheel reset boundary",
    )
    save_evidence(root, "permission-wheel-burst-governed", deliberate_wheel)
    send_keys(tmux_exe, session, "R", "Enter")
    wait_for_absent(tmux_exe, session, r"Permission required", "permission prompt denied")
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "100", "-y", "32")
    denied_card = wait_for(
        tmux_exe,
        session,
        r"(?s)x bash · command permission denied.*\$ <redacted one-shot command>",
        "permission denial Rich tool card",
    )
    denied_card = wait_for_absent(
        tmux_exe,
        session,
        r"~ bash · <redacted one-shot command>",
        "stale running permission tool-card row",
    )
    assert_f5_frame(denied_card, 100, 32, "denied Rich tool card")
    denied_primary_rows = [line for line in denied_card.splitlines() if re.search(r"[~+x] bash ·", line)]
    if (
        len(denied_primary_rows) != 1
        or "x bash · command permission denied" not in denied_primary_rows[0]
        or "permission: deny" in denied_card
        or "permreq_" in denied_card
        or "resolver:" in denied_card
    ):
        raise RuntimeError(f"permission denial did not render one human-readable Rich card without routine audit receipts\nscreen:\n{denied_card}")
    save_evidence(root, "frontend-f5-denied-tool-card", denied_card)
    save_evidence(root, "permission-denied-tool-card", denied_card)

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/permissions list")
    send_keys(tmux_exe, session, "Enter")
    remembered_rule = wait_for(
        tmux_exe,
        session,
        r'(?s)permrule_.*deny bash.*command="git push origin main"',
        "remembered permission rule listing",
    )
    if 'command="git push origin main"' not in remembered_rule or "deny bash" not in remembered_rule:
        raise RuntimeError(f"remembered exact critical-command deny rule was not listed\nscreen:\n{remembered_rule}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/bash git push origin main")
    send_keys(tmux_exe, session, "Enter")
    repeated_denial = wait_for(
        tmux_exe,
        session,
        r"(?s)/bash git push origin main.*x bash\s+·\s+command permission denied.*\$ <redacted one-shot command>",
        "remembered denial result",
    )
    if "Permission required" in repeated_denial or "PERMISSION REQUIRED" in repeated_denial:
        raise RuntimeError(f"remembered deny rule did not suppress a repeated prompt\nscreen:\n{repeated_denial}")
    send_keys(tmux_exe, session, "C-o")
    expanded_tool_details = wait_for(
        tmux_exe,
        session,
        r"(?s)x bash · command permission denied.*\$ <redacted one-shot command>",
        "ctrl-o safe tool detail expansion",
    )
    if any(token in expanded_tool_details for token in ("permission: deny", "id: permreq_", "resolver:", "inspect: /permissions", "diagnose: /permissions")):
        raise RuntimeError(f"Ctrl+O leaked routine permission audit receipts into tool details\nscreen:\n{expanded_tool_details}")
    save_evidence(root, "permission-denied-expanded-details", expanded_tool_details)
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "56", "-y", "28")
    narrow_plain_permission = wait_for(
        tmux_exe,
        session,
        r"(?s)command permission denied.*\$ <redacted one-shot command>",
        "narrow plain permission detail rows",
    )
    if "permreq_" in narrow_plain_permission or "permission: deny" in narrow_plain_permission:
        raise RuntimeError(f"narrow permission details leaked routine audit receipts\nscreen:\n{narrow_plain_permission}")
    save_evidence(root, "permission-denied-narrow-no-color", narrow_plain_permission)
    styled_narrow_permission = capture_styled(tmux_exe, session)
    if "\x1b[" in styled_narrow_permission:
        raise RuntimeError(f"narrow NO_COLOR permission details still captured ANSI style escapes\nscreen:\n{styled_narrow_permission}")
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "120", "-y", "32")
    wait_for(tmux_exe, session, r"\$ <redacted one-shot command>", "permission detail rows after resize restore")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/copy tool")
    send_keys(tmux_exe, session, "Enter")
    copied_tool = wait_for(tmux_exe, session, r"copied latest tool details to clipboard", "copy latest tool details")
    if "copied latest tool details to clipboard" not in copied_tool:
        raise RuntimeError(f"/copy tool did not report a copied tool-detail payload\nscreen:\n{copied_tool}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/permissions audit bash")
    send_keys(tmux_exe, session, "Enter")
    permission_audit_listing = wait_for(tmux_exe, session, r"(?s)Permission audit.*permreq_", "explicit permission audit listing")
    permission_request_match = re.search(r"permreq_[A-Za-z0-9_]+", permission_audit_listing)
    if not permission_request_match:
        raise RuntimeError(f"explicit audit surface did not expose a reusable permission request id\nscreen:\n{permission_audit_listing}")
    permission_request_prefix = permission_request_match.group(0)

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, f"/copy permission {permission_request_prefix}")
    send_keys(tmux_exe, session, "Enter")
    copied_permission = wait_for(
        tmux_exe,
        session,
        r"copied matching permission details to clipboard",
        "copy matching permission details",
    )
    if "copied matching permission details to clipboard" not in copied_permission:
        raise RuntimeError(f"/copy permission did not report a copied audit payload\nscreen:\n{copied_permission}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/write src/main.cpp int changed() { return 1; }")
    send_keys(tmux_exe, session, "Enter")
    write_result = wait_for(
        tmux_exe,
        session,
        r"wrote 27 bytes|Permission required",
        "write command result for diff copy",
    )
    if "Permission required" in write_result or "PERMISSION REQUIRED" in write_result:
        send_keys(tmux_exe, session, "Tab", "Enter")
        write_result = wait_for(tmux_exe, session, r"wrote 27 bytes", "allowed write command result")
    if "wrote 27 bytes" not in write_result:
        raise RuntimeError(f"/write did not render a successful mutation tool card\nscreen:\n{write_result}")
    save_evidence(root, "write-tool-card-success", write_result)
    write_changed_details = wait_for(
        tmux_exe,
        session,
        r"changed:",
        "write changed-file detail row",
    )
    if "changed:" not in write_changed_details:
        raise RuntimeError(
            f"/write did not render the changed-file summary row in expanded tool details\nscreen:\n{write_changed_details}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/details compact")
    send_keys(tmux_exe, session, "Enter")
    wait_for_absent(tmux_exe, session, r"changed:", "write detail rows cleared before mouse parity")
    collapsed_before_mouse = wait_for(
        tmux_exe,
        session,
        r"\+ write.*wrote 27 bytes",
        "write card collapsed before mouse parity",
    )
    write_header = next(
        ((index + 1, line) for index, line in enumerate(collapsed_before_mouse.splitlines()) if "+ write" in line),
        None,
    )
    if write_header is None:
        raise RuntimeError(f"collapsed write card did not expose a mouse target\nscreen:\n{collapsed_before_mouse}")
    write_header_row, write_header_text = write_header
    write_header_column = write_header_text.index("+ write") + 1
    send_literal(tmux_exe, session, f"\x1b[<0;{write_header_column};{write_header_row}M")
    mouse_expanded = wait_for(
        tmux_exe,
        session,
        r"(?s)\+ write.*changed:.*diff src/main.cpp:",
        "write card mouse expansion",
    )
    if (
        len([line for line in mouse_expanded.splitlines() if "+ write" in line]) != 1
        or mouse_expanded.count("src/main.cpp · wrote 27 bytes") != 1
        or mouse_expanded.count("wrote 27 bytes") != 2
        or "result: wrote 27 bytes to " not in mouse_expanded
    ):
        raise RuntimeError(f"mouse-expanded write card duplicated payloads or mislabeled actions\nscreen:\n{mouse_expanded}")
    save_evidence(root, "frontend-f5-tool-card-mouse-expanded", mouse_expanded)
    expanded_write_header = next(
        ((index + 1, line) for index, line in enumerate(mouse_expanded.splitlines()) if "+ write" in line),
        None,
    )
    if expanded_write_header is None:
        raise RuntimeError(f"expanded write card lost its clickable header\nscreen:\n{mouse_expanded}")
    expanded_header_row, expanded_header_text = expanded_write_header
    expanded_header_column = expanded_header_text.index("+ write") + 1
    send_literal(tmux_exe, session, f"\x1b[<0;{expanded_header_column};{expanded_header_row}M")
    wait_for_absent(tmux_exe, session, r"changed:", "write card mouse collapse")
    mouse_collapsed = wait_for(tmux_exe, session, r"\+ write.*wrote 27 bytes", "write card mouse collapse settled")
    if len([line for line in mouse_collapsed.splitlines() if "+ write" in line]) != 1:
        raise RuntimeError(f"mouse collapse duplicated the write card\nscreen:\n{mouse_collapsed}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/tool write")
    send_keys(tmux_exe, session, "Enter")
    tool_toggled_visible = wait_for(
        tmux_exe,
        session,
        r"(?s)\+ write.*changed:",
        "truthful /tool write expansion",
    )
    if len([line for line in tool_toggled_visible.splitlines() if "+ write" in line]) != 1:
        raise RuntimeError(f"/tool expansion appended a duplicate write card\nscreen:\n{tool_toggled_visible}")

    def assert_f2_tool_frame(
        screen: str,
        width: int,
        height: int,
        label: str,
        *,
        expanded: bool,
        sidebar_expected: bool,
    ) -> None:
        dimensions = tmux(
            tmux_exe, "display-message", "-p", "-t", session, "#{window_width},#{window_height}"
        ).stdout.strip()
        if dimensions != f"{width},{height}":
            raise RuntimeError(f"{label} dimensions were {dimensions}, expected {width},{height}")
        lines = screen.splitlines()
        if len(lines) != height:
            raise RuntimeError(f"{label} had {len(lines)} rows, expected {height}\nscreen:\n{screen}")
        expected_result_count = 2 if expanded else 1
        if "+ write" not in screen or screen.count("wrote 27 bytes") != expected_result_count:
            raise RuntimeError(
                f"{label} did not keep one write shell with the expected exact payload details\nscreen:\n{screen}"
            )
        primary_rows = [line for line in screen.splitlines() if "+ write" in line]
        if not primary_rows or "src/main.cpp · wrote 27 bytes" not in primary_rows[0] or primary_rows[0].count("src/main.cpp") != 1:
            raise RuntimeError(f"{label} did not carry one workspace-relative write target into the collapsed primary row\nscreen:\n{screen}")
        if "/src/main.cpp" in screen:
            raise RuntimeError(f"{label} leaked an absolute write target\nscreen:\n{screen}")
        if not expanded and re.search(r"result:\s*wrote 27 bytes", screen):
            raise RuntimeError(f"{label} retained an expanded result row while collapsed\nscreen:\n{screen}")
        if expanded:
            if (
                "changed:" not in screen
                or "diff" not in screen
                or "+int changed()" not in screen
                or "result: wrote 27 bytes to " not in screen
            ):
                raise RuntimeError(f"{label} did not retain deduplicated expanded changed/diff detail\nscreen:\n{screen}")
        elif "changed:" in screen or "+int changed()" in screen:
            raise RuntimeError(f"{label} retained expanded changed/diff detail after collapse\nscreen:\n{screen}")
        if any(len(line) > width for line in lines):
            raise RuntimeError(f"{label} exceeded the {width}-column capture bound\nscreen:\n{screen}")
        unexpected_controls = [character for character in screen if ord(character) < 32 and character != "\n"]
        if unexpected_controls or "\x1b" in screen:
            raise RuntimeError(f"{label} contained ESC or unexpected C0 controls\nscreen:\n{screen}")

        main_width = width - 39 if sidebar_expected else min(width, 120)
        canvas_left = 0 if sidebar_expected else (width - main_width) // 2
        if not lines[-2][canvas_left : canvas_left + main_width].startswith("│  Type a message..."):
            raise RuntimeError(f"{label} did not keep the composer on the penultimate row\nscreen:\n{screen}")
        if not lines[-1][canvas_left : canvas_left + main_width].startswith("│  GPT-5.5"):
            raise RuntimeError(f"{label} did not keep the quiet footer on the final row\nscreen:\n{screen}")
        if sidebar_expected:
            if any(len(line) <= main_width or line[main_width] != "│" for line in lines):
                raise RuntimeError(f"{label} did not retain the curated F1 rail divider\nscreen:\n{screen}")
            rail_text = "\n".join(line[main_width + 1 :] for line in lines)
            if "  Session" not in rail_text or "build · openai/GPT-5.5" not in rail_text:
                raise RuntimeError(f"{label} did not retain the curated F1 Session rail\nscreen:\n{screen}")
            if "live session" in rail_text or "AVA" in rail_text or "path " in rail_text or "entries " in rail_text:
                raise RuntimeError(f"{label} rail regressed to branding or raw metadata\nscreen:\n{screen}")
        elif any(len(line) > width for line in lines) or "  Session" in "\n".join(lines[-2:]):
            raise RuntimeError(f"{label} did not remain within the centered canvas without the automatic rail\nscreen:\n{screen}")
        if width == 160 and not sidebar_expected and canvas_left != 20:
            raise RuntimeError(f"{label} did not use the exact 20-column centered inset\nscreen:\n{screen}")

        if height <= 12:
            shell_row = next((index for index, line in enumerate(lines) if "+ write" in line), None)
            if shell_row is None or "src/main.cpp · wrote 27 bytes" not in lines[shell_row]:
                raise RuntimeError(f"{label} did not keep the relative target and compact result on its short-height tool header\nscreen:\n{screen}")

    def resize_and_capture_f2(
        width: int,
        height: int,
        name: str,
        *,
        expanded: bool,
        sidebar_expected: bool,
    ) -> str:
        previous = capture(tmux_exe, session)
        tmux(tmux_exe, "resize-window", "-t", session, "-x", str(width), "-y", str(height))
        if capture(tmux_exe, session) == previous:
            wait_for_screen_change(tmux_exe, session, previous, f"{name} resize redraw")
        screen = wait_for(
            tmux_exe,
            session,
            rf"(?s)\+ write.*wrote 27 bytes.*Type a message.*GPT-5\.5 · ctx {ACTIVE_CONTEXT_STATUS_PATTERN}[^\n]*\Z",
            f"{name} settled tool transcript",
        )
        assert_f2_tool_frame(
            screen,
            width,
            height,
            name,
            expanded=expanded,
            sidebar_expected=sidebar_expected,
        )
        save_evidence(root, name, screen)
        return screen

    resize_and_capture_f2(
        120,
        36,
        "frontend-f2-tool-shell-expanded-ordinary",
        expanded=True,
        sidebar_expected=False,
    )
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/tool write")
    send_keys(tmux_exe, session, "Enter")
    tool_toggled_compact = wait_for_absent(tmux_exe, session, r"changed:", "truthful /tool write collapse")
    if len([line for line in tool_toggled_compact.splitlines() if "+ write" in line]) != 1:
        raise RuntimeError(f"/tool collapse status was untruthful or duplicated the write card\nscreen:\n{tool_toggled_compact}")
    resize_and_capture_f2(160, 48, "frontend-f2-transcript-wide", expanded=False, sidebar_expected=False)
    resize_and_capture_f2(120, 36, "frontend-f2-transcript-ordinary", expanded=False, sidebar_expected=False)
    resize_and_capture_f2(80, 24, "frontend-f2-transcript-narrow", expanded=False, sidebar_expected=False)
    resize_and_capture_f2(100, 12, "frontend-f2-transcript-short", expanded=False, sidebar_expected=False)

    restore_f2_previous = capture(tmux_exe, session)
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "120", "-y", "32")
    wait_for_screen_change(tmux_exe, session, restore_f2_previous, "F2 permission-flow baseline restore")
    restored_f2_details = wait_for(tmux_exe, session, r"\+ write.*wrote 27 bytes", "F2 compact details restored")
    if "+ write" not in restored_f2_details or restored_f2_details.count("wrote 27 bytes") != 1 or "changed:" in restored_f2_details:
        raise RuntimeError(f"F2 restore did not preserve the deduplicated compact write card\nscreen:\n{restored_f2_details}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/copy diff")
    send_keys(tmux_exe, session, "Enter")
    copied_diff = wait_for(tmux_exe, session, r"copied latest tool diff to clipboard", "copy latest tool diff")
    if "copied latest tool diff to clipboard" not in copied_diff:
        raise RuntimeError(f"/copy diff did not report a copied unified diff\nscreen:\n{copied_diff}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/copy diff main.cpp")
    send_keys(tmux_exe, session, "Enter")
    copied_matching_diff = wait_for(
        tmux_exe, session, r"copied matching tool diff to clipboard", "copy matching tool diff"
    )
    if "copied matching tool diff to clipboard" not in copied_matching_diff:
        raise RuntimeError(f"/copy diff <query> did not report a copied matching diff\nscreen:\n{copied_matching_diff}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/diff main.cpp")
    send_keys(tmux_exe, session, "Enter")
    visible_matching_diff = wait_for(
        tmux_exe,
        session,
        r"(?s)Matching tool diff:.*\+int changed\(\)",
        "visible matching tool diff",
    )
    if "Matching tool diff:" not in visible_matching_diff or "+int changed()" not in visible_matching_diff:
        raise RuntimeError(f"/diff <query> did not render the matching unified diff\nscreen:\n{visible_matching_diff}")
    save_evidence(root, "visible-diff-card", visible_matching_diff)

    save_evidence(root, "visible-tool-details", tool_toggled_visible)

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/copy tool write")
    send_keys(tmux_exe, session, "Enter")
    copied_matching_tool = wait_for(
        tmux_exe, session, r"copied matching tool details to clipboard", "copy matching tool details"
    )
    if "copied matching tool details to clipboard" not in copied_matching_tool:
        raise RuntimeError(f"/copy tool <query> did not report a copied matching tool\nscreen:\n{copied_matching_tool}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, f"/permissions audit {permission_request_prefix}")
    send_keys(tmux_exe, session, "Enter")
    permission_audit = wait_for(
        tmux_exe,
        session,
        r"(?s)request=permreq_.*<redacted one-shot command>",
        "permission audit command output",
    )
    if "request=permreq_" not in permission_audit or "<redacted one-shot command>" not in permission_audit:
        raise RuntimeError(f"permission audit command did not render the redacted denied command\nscreen:\n{permission_audit}")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, f"/permissions audit summary {permission_request_prefix}")
    send_keys(tmux_exe, session, "Enter")
    permission_audit_summary = wait_for(
        tmux_exe,
        session,
        r"(?s)Permission audit summary:.*denials: [1-9].*by resolution: deny=",
        "permission audit summary command output",
    )
    if (
        "Permission audit summary:" not in permission_audit_summary
        or "denials: " not in permission_audit_summary
        or "by resolution: deny=" not in permission_audit_summary
    ):
        raise RuntimeError(
            f"permission audit summary command did not render grouped audit counts\nscreen:\n{permission_audit_summary}"
        )
    save_evidence(root, "permission-audit-summary", permission_audit_summary)

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, f"/permissions audit export {permission_request_prefix}")
    send_keys(tmux_exe, session, "Enter")
    permission_audit_export = wait_for(
        tmux_exe,
        session,
        r"(?s)<redacted one-shot command>.*```",
        "permission audit export command output",
    )
    if "```" not in permission_audit_export or "<redacted one-shot command>" not in permission_audit_export:
        raise RuntimeError(
            f"permission audit export command did not render the redacted denied command\nscreen:\n{permission_audit_export}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, f"/permissions diagnose {permission_request_prefix}")
    send_keys(tmux_exe, session, "Enter")
    permission_diagnostics = wait_for(
        tmux_exe,
        session,
        r"(?s)Recent permission denials:.*<redacted one-shot command>",
        "permission denial diagnostics command output",
    )
    if "Recent permission denials:" not in permission_diagnostics or "<redacted one-shot command>" not in permission_diagnostics:
        raise RuntimeError(
            f"permission denial diagnostics command did not explain the redacted denied command\nscreen:\n{permission_diagnostics}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, f"/permissions audit show {permission_request_prefix}")
    send_keys(tmux_exe, session, "Enter")
    permission_audit_detail = wait_for(
        tmux_exe,
        session,
        r"(?s)command: <redacted one-shot command>.*Related commands",
        "permission audit detail command output",
    )
    if "command: <redacted one-shot command>" not in permission_audit_detail or "Related commands" not in permission_audit_detail:
        raise RuntimeError(
            f"permission audit detail command did not render the redacted denied command\nscreen:\n{permission_audit_detail}"
        )

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/bash seq 1 20000")
    send_keys(tmux_exe, session, "Enter")
    wait_for(
        tmux_exe,
        session,
        r"Permission required",
        "bounded local bash spill permission",
        timeout=12.0,
    )
    send_literal(tmux_exe, session, "a")
    seq_result = wait_for(
        tmux_exe,
        session,
        r"(?s)\+ bash.*exit 0",
        "allowed bounded local bash spill settled",
        timeout=30.0,
    )
    if re.search(r"(?m)^  (?:19999|20000)\s*│", seq_result):
        raise RuntimeError(f"compact local spill card leaked the retained raw output instead of its bounded summary\nscreen:\n{seq_result}")

    send_keys(tmux_exe, session, "C-o")
    seq_expanded = wait_for(
        tmux_exe,
        session,
        r"(?s)truncation:.*full output:",
        "expanded bounded local bash spill details",
        timeout=10.0,
    )
    if (
        "… 19800 lines hidden" not in seq_expanded
        or "20001 lines" in seq_expanded
        or "19801 lines hidden" in seq_expanded
        or "truncation:" not in seq_expanded
        or "bytes" not in seq_expanded
        or "full output:" not in seq_expanded
    ):
        raise RuntimeError(f"expanded local spill card lacked bounded truthful metadata\nscreen:\n{seq_expanded}")

    def resize_and_capture_lifecycle(width: int, height: int, name: str, *, sidebar_expected: bool) -> None:
        previous = capture(tmux_exe, session)
        tmux(tmux_exe, "resize-window", "-t", session, "-x", str(width), "-y", str(height))
        if capture(tmux_exe, session) == previous:
            wait_for_screen_change(tmux_exe, session, previous, f"{name} resize redraw")
        lifecycle = wait_for(
            tmux_exe,
            session,
            r"(?s)truncation:.*full output:.*GPT-5\.5[^\n]*\n?\Z",
            f"{name} lifecycle frame",
        )
        dimensions = tmux(
            tmux_exe, "display-message", "-p", "-t", session, "#{window_width},#{window_height}"
        ).stdout.strip()
        lines = lifecycle.splitlines()
        if dimensions != f"{width},{height}" or len(lines) != height or any(len(line) > width for line in lines):
            raise RuntimeError(f"{name} did not retain exact bounded dimensions\nscreen:\n{lifecycle}")
        if "full output:" not in lifecycle or "truncation:" not in lifecycle:
            raise RuntimeError(f"{name} lost spill or truncation metadata\nscreen:\n{lifecycle}")
        if "\x1b" in lifecycle or any(ord(character) < 32 and character != "\n" for character in lifecycle):
            raise RuntimeError(f"{name} contained ESC or unexpected C0 controls\nscreen:\n{lifecycle}")
        main_width = width - 39 if sidebar_expected else min(width, 120)
        canvas_left = 0 if sidebar_expected else (width - main_width) // 2
        if not lines[-2][canvas_left : canvas_left + main_width].startswith("│  Type a message...") or not lines[-1][
            canvas_left : canvas_left + main_width
        ].startswith("│  GPT-5.5"):
            raise RuntimeError(f"{name} lost the docked composer/footer geometry\nscreen:\n{lifecycle}")
        if sidebar_expected and any(len(line) <= main_width or line[main_width] != "│" for line in lines):
            raise RuntimeError(f"{name} lost the automatic rail divider\nscreen:\n{lifecycle}")
        save_evidence(root, name, lifecycle)

    resize_and_capture_lifecycle(160, 48, "frontend-f5-lifecycle-wide", sidebar_expected=False)
    resize_and_capture_lifecycle(120, 36, "frontend-f5-lifecycle-ordinary", sidebar_expected=False)
    resize_and_capture_lifecycle(80, 24, "frontend-f5-lifecycle-narrow", sidebar_expected=False)
    resize_and_capture_lifecycle(100, 12, "frontend-f5-lifecycle-short", sidebar_expected=False)

    _finish_main(tmux_exe, session)
