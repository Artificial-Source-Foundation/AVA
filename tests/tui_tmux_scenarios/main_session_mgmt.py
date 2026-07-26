"""The tmux TUI smoke scenario for main session mgmt."""

from __future__ import annotations

from tui_smoke_helpers import (
    SmokeContext,
    assert_screen_absent_for,
    assert_screen_present_for,
    capture,
    save_evidence,
    send_keys,
    send_literal,
    wait_for,
    wait_for_absent,
    wait_for_screen_change,
)
from .common import (
    _finish_main,
    _main_session,
    assert_title_first_new_receipt,
)


def scenario_main_session_mgmt(ctx: SmokeContext) -> None:
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)
    # This scenario cannot rely on prior scenarios for transcript rows.
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/help")
    wait_for(tmux_exe, session, r"/help", "session-management scrollback seed draft")
    send_keys(tmux_exe, session, "Enter")
    wait_for(
        tmux_exe,
        session,
        r"page_up PageUp|model_cycle_forward|details_toggle|tree_fold_or_up|tree_unfold_or_down",
        "session-management deterministic scrollback seed output",
    )
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/name TUI smoke")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"session name set: \"TUI smoke\"", "session name command")
    send_keys(tmux_exe, session, "Up")
    arrow_scrollback = wait_for(tmux_exe, session, r"scrollback detached", "idle Up arrow transcript scrolling")
    if "│  /name TUI smoke" in arrow_scrollback:
        raise RuntimeError(
            "idle Up arrow recalled composer input history instead of only scrolling transcript history\n"
            f"screen:\n{arrow_scrollback}"
        )
    send_keys(tmux_exe, session, "Down")
    wait_for_absent(tmux_exe, session, r"scrollback detached", "idle Down arrow return to live tail")

    for index in range(1, 7):
        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, f"/new Page {index}")
        send_keys(tmux_exe, session, "Enter")
        previous_title = "TUI smoke" if index == 1 else f"Page {index - 1}"
        new_receipt = wait_for(
            tmux_exe,
            session,
            rf'(?s)started session "Page {index}" · id.*?session_.*previous session "{previous_title}" · id.*?session_.*switched to "Page {index}"',
            f"seed page session {index}",
        )
        assert_title_first_new_receipt(new_receipt, f"Page {index}", previous_title, f"/new Page {index}")
        if index == 1:
            save_evidence(root, "session-new-title-first-receipt", new_receipt)

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette row")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/resume.*Resume a session", "resume palette dismissed")
    send_keys(tmux_exe, session, "Enter")
    selector = wait_for(tmux_exe, session, r"Select session|Session tree", "resume session selector")
    if "session selector opened" not in selector and "Select session" not in selector:
        raise RuntimeError(f"/resume did not open the session selector\nscreen:\n{selector}")
    save_evidence(root, "session-selector", selector)
    send_literal(tmux_exe, session, "\x1b[6~")
    page_down = wait_for(tmux_exe, session, r"›\s+Page 1", "session selector page down")
    if "Page 1" not in page_down:
        raise RuntimeError(f"session selector PageDown did not jump by a page\nscreen:\n{page_down}")
    send_literal(tmux_exe, session, "\x1b[5~")
    page_up = wait_for(tmux_exe, session, r"›\s+(?:●\s+)?Page 6", "session selector page up")
    if "Page 6" not in page_up:
        raise RuntimeError(f"session selector PageUp did not jump by a page\nscreen:\n{page_up}")
    send_literal(tmux_exe, session, "tui smoke")
    wait_for(tmux_exe, session, r"(?s)filter\s+tui smoke█.*›\s+TUI smoke", "session selector query after page navigation")
    send_keys(tmux_exe, session, "C-s")
    wait_for(tmux_exe, session, r"sort name|Ctrl\+S/Ctrl\+T sort \(name\)", "session selector sort cycle")
    send_keys(tmux_exe, session, "C-n")
    named_filter = wait_for(tmux_exe, session, r"sort name · named", "session selector named-only filter")
    if "TUI smoke" not in named_filter or "sort name · named" not in named_filter:
        raise RuntimeError(f"session selector named-only filter did not keep the named session visible\nscreen:\n{named_filter}")
    named_lines = named_filter.splitlines()
    named_start = next((index for index, line in enumerate(named_lines) if "Select session" in line), None)
    named_end = next((index for index, line in enumerate(named_lines) if index >= (named_start or 0) and "Ctrl+D archive" in line), None)
    named_modal = "\n".join(named_lines[named_start : named_end + 1]) if named_start is not None and named_end is not None else ""
    runtime_state_root = str(ctx.state.parent)
    if (
        not named_modal
        or "session_" in named_modal
        or ".jsonl" in named_modal
        or runtime_state_root in named_modal
        or "current current" in named_modal
    ):
        raise RuntimeError(f"default session selector rows exposed ids, paths, or duplicate current state\nscreen:\n{named_filter}")
    save_evidence(root, "session-selector-named-default-path-hidden", named_filter)
    send_keys(tmux_exe, session, "C-p")
    path_toggle = wait_for(tmux_exe, session, r"sort name · named · paths", "session selector path-display toggle")
    if "TUI smoke" not in path_toggle or "sort name · named · paths" not in path_toggle:
        raise RuntimeError(f"session selector path-display toggle did not keep the named session visible\nscreen:\n{path_toggle}")
    path_lines = path_toggle.splitlines()
    path_start = next((index for index, line in enumerate(path_lines) if "Select session" in line), None)
    path_end = next((index for index, line in enumerate(path_lines) if index >= (path_start or 0) and "Ctrl+D archive" in line), None)
    path_modal = "\n".join(path_lines[path_start : path_end + 1]) if path_start is not None and path_end is not None else ""
    if runtime_state_root not in path_modal and ".jsonl" not in path_modal:
        raise RuntimeError(f"Ctrl+P did not explicitly disclose the selected session path\nscreen:\n{path_toggle}")
    save_evidence(root, "session-selector-path-disclosed", path_toggle)
    send_keys(tmux_exe, session, "C-r")
    rename_draft = wait_for(tmux_exe, session, r"/sessions rename session_", "session selector rename draft")
    if "/sessions rename session_" not in rename_draft:
        raise RuntimeError(f"session selector Ctrl+R did not restore a rename command draft\nscreen:\n{rename_draft}")
    send_literal(tmux_exe, session, "Selector rename")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"session .* name set: \"Selector rename\"", "session selector rename command")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before label draft")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/resume.*Resume a session", "resume palette dismissed before label draft")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before label draft")
    send_literal(tmux_exe, session, "Selector rename")
    wait_for(tmux_exe, session, r"›\s+Selector rename", "resume selector filtered before label draft")
    send_literal(tmux_exe, session, "L")
    labels_draft = wait_for(tmux_exe, session, r"/sessions labels session_", "session selector Shift+L labels draft")
    if "/sessions labels session_" not in labels_draft:
        raise RuntimeError(f"session selector Shift+L did not restore a labels command draft\nscreen:\n{labels_draft}")
    send_literal(tmux_exe, session, "picker bookmark")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"session .* labels set: picker,bookmark", "session selector labels command")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/sessions picker")
    # Post-submit state refreshes the dynamic session completion catalog. Wait
    # for the named row itself before dismissing it; visible rows intentionally
    # no longer expose the canonical session id.
    wait_for(tmux_exe, session, r"│\s+›\s+Selector rename", "literal sessions query completion active")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"│\s+›\s+Selector rename", "literal sessions query completion dismissed")
    send_keys(tmux_exe, session, "Enter")
    wait_for(
        tmux_exe,
        session,
        r"(?s)Sessions:.*Selector rename.*labels=picker,bookmark",
        "session selector labels visible in tree",
    )
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before label-time toggle")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/resume.*Resume a session", "resume palette dismissed before label-time toggle")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before label-time toggle")
    send_literal(tmux_exe, session, "Selector rename")
    wait_for(tmux_exe, session, r"›\s+Selector rename", "resume selector filtered before label-time toggle")
    send_literal(tmux_exe, session, "T")
    label_time = wait_for(tmux_exe, session, r"label times", "session selector Shift+T label-time toggle")
    if "Selector rename" not in label_time:
        raise RuntimeError(f"session selector Shift+T lost the filtered labeled row\nscreen:\n{label_time}")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Select session|Session tree", "session selector closed after label-time toggle")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/new Archive current")
    send_keys(tmux_exe, session, "Enter")
    archive_new_receipt = wait_for(
        tmux_exe,
        session,
        r'(?s)started session "Archive current" · id.*?session_.*previous session "Page 6" · id.*?session_.*switched to "Archive current"',
        "new session before selector archive",
    )
    assert_title_first_new_receipt(archive_new_receipt, "Archive current", "Page 6", "archive setup /new")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before archive")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/resume.*Resume a session", "resume palette dismissed before archive")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before archive")
    send_literal(tmux_exe, session, "Selector rename")
    wait_for(tmux_exe, session, r"›\s+Selector rename", "session selector non-current row selected")
    send_literal(tmux_exe, session, "\x1b[127;5u")
    ctrl_backspace_filtered = assert_screen_absent_for(
        tmux_exe,
        session,
        r"press Ctrl+Backspace again|press Ctrl+D again",
        "Ctrl+Backspace archive confirmation while the selector query was non-empty",
    )
    if (
        "press Ctrl+Backspace again" in ctrl_backspace_filtered
        or "press Ctrl+D again" in ctrl_backspace_filtered
    ):
        raise RuntimeError(
            "Ctrl+Backspace opened archive confirmation while the selector query was non-empty\n"
            f"screen:\n{ctrl_backspace_filtered}"
        )
    for _ in range(len("Selector rename")):
        send_literal(tmux_exe, session, "\x1b[127;2u")
    wait_for(tmux_exe, session, r"›\s+Selector rename", "session selector row selected after clearing query")
    send_literal(tmux_exe, session, "\x1b[127;5u")
    ctrl_backspace_confirmation = assert_screen_present_for(
        tmux_exe,
        session,
        r"Select session|Session tree",
        "Ctrl+Backspace first archive press closed the selector instead of waiting for confirmation",
    )
    if "Select session" not in ctrl_backspace_confirmation and "Session tree" not in ctrl_backspace_confirmation:
        raise RuntimeError(
            "Ctrl+Backspace first archive press closed the selector instead of waiting for confirmation\n"
            f"screen:\n{ctrl_backspace_confirmation}"
        )
    archived_selector_before = capture(tmux_exe, session)
    send_literal(tmux_exe, session, "\x1b[127;5u")
    wait_for_screen_change(tmux_exe, session, archived_selector_before, "selector archive completion")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"Select session|Session tree", "session selector closed after archive")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/sessions --archived Selector rename")
    send_keys(tmux_exe, session, "Enter")
    archived_sessions = wait_for(
        tmux_exe, session, r"(?s)Sessions \(including archived\):.*Selector rename", "archived session list"
    )
    if "archived" not in archived_sessions:
        raise RuntimeError(f"Archived session list did not mark the archived row\nscreen:\n{archived_sessions}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before restore")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"/resume.*Resume a session", "resume palette dismissed before restore")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before restore")
    send_keys(tmux_exe, session, "C-a")
    archived_selector = wait_for(
        tmux_exe, session, r"Select session\s+sort recent · archived", "session selector archived toggle"
    )
    send_literal(tmux_exe, session, "Selector rename")
    archived_selector = wait_for(
        tmux_exe, session, r"(?m)^\s*›\s+Selector rename\s+archived", "archived row filtered in selector"
    )
    if "archived" not in archived_selector:
        raise RuntimeError(f"session selector did not show archived session state\nscreen:\n{archived_selector}")
    send_keys(tmux_exe, session, "C-d")
    assert_screen_present_for(
        tmux_exe,
        session,
        r"(?m)^\s*›\s+Selector rename\s+archived",
        "selector restore confirmation",
    )
    send_keys(tmux_exe, session, "C-d")
    wait_for_absent(
        tmux_exe,
        session,
        r"(?m)^\s*›\s+Selector rename\s+archived",
        "selector restore completion",
    )
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"Select session|Session tree", "session selector closed after restore")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/sessions Selector rename")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"(?s)Sessions:.*Selector rename", "restored session visible in default list")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/name Branch parent")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"session name set: \"Branch parent\"", "branch parent session name")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/fork Branch child")
    send_keys(tmux_exe, session, "Enter")
    wait_for(
        tmux_exe,
        session,
        r"(?s)forked session session_.*name=\"Branch child\".*switched to session_",
        "forked child session before selector branch navigation",
    )
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before parent branch navigation")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(
        tmux_exe,
        session,
        r"/resume.*Resume a session",
        "resume palette dismissed before parent branch navigation",
    )
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before parent branch navigation")
    send_literal(tmux_exe, session, "\x1b[1;3D")
    wait_for(tmux_exe, session, r"opened parent branch session_", "selector alt-left opened parent branch")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/sessions Branch parent")
    send_keys(tmux_exe, session, "Enter")
    parent_active = wait_for(
        tmux_exe,
        session,
        r"(?s)Sessions:.*\* Branch parent",
        "parent branch active after selector alt-left",
    )
    if "* Branch parent" not in parent_active:
        raise RuntimeError(f"selector Alt+Left did not make the parent branch current\nscreen:\n{parent_active}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/resume")
    wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before child branch navigation")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(
        tmux_exe,
        session,
        r"/resume.*Resume a session",
        "resume palette dismissed before child branch navigation",
    )
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before child branch navigation")
    send_literal(tmux_exe, session, "\x1b[1;3C")
    wait_for(tmux_exe, session, r"opened child branch session_", "selector alt-right opened child branch")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/sessions Branch child")
    send_keys(tmux_exe, session, "Enter")
    child_active = wait_for(
        tmux_exe,
        session,
        r"(?s)Sessions:.*\* Branch child",
        "child branch active after selector alt-right",
    )
    if "* Branch child" not in child_active:
        raise RuntimeError(f"selector Alt+Right did not make the child branch current\nscreen:\n{child_active}")

    _finish_main(tmux_exe, session)
