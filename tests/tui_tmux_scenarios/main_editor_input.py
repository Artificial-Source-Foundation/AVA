"""The tmux TUI smoke scenario for main editor input."""

from __future__ import annotations

from tui_smoke_helpers import (
    SmokeContext,
    pane_cursor_position,
    send_keys,
    send_literal,
    wait_for,
    wait_for_absent,
    wait_for_cursor_change,
    wait_for_pane_command,
)
from .common import (
    _finish_main,
    _main_session,
    assert_title_first_new_receipt,
)


def scenario_main_editor_input(ctx: SmokeContext) -> None:
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)
    (ava_config / "keybinds.json").write_text(
        '{"tui.editor.cursorLineStart":["Home","Ctrl+A","Alt+Up","Insert"],'
        '"tui.editor.cursorLineEnd":["F2","Ctrl+1"],'
        '"tui.editor.cursorLeft":["Left","Ctrl+B","Alt+H"],'
        '"tui.editor.cursorWordRight":["Ctrl+Right","Alt+Right","Alt+F","Alt+W"],'
        '"tui.editor.deleteCharBackward":["Shift+Backspace","Ctrl+H"],'
        '"tui.editor.deleteCharForward":["Shift+Delete","Delete"],'
        '"app.session.resume":"Alt+J",'
        '"app.session.new":"Alt+K",'
        '"tui.select.confirm":["Enter","Space"],'
        '"tui.select.cancel":["Escape","Ctrl+W"]}\n',
        encoding="utf-8",
    )
    send_literal(tmux_exe, session, "/reload")
    wait_for(tmux_exe, session, r"Reload config domains", "reload palette description")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Reload config domains", "reload palette description dismissed")
    send_keys(tmux_exe, session, "Enter")
    reload_screen = wait_for(tmux_exe, session, r"keybindings reloaded", "live keybinding reload")
    if "keybindings reloaded" not in reload_screen:
        raise RuntimeError(f"/reload did not report a live keybinding reload\nscreen:\n{reload_screen}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "fkey")
    send_keys(tmux_exe, session, "C-a")
    send_keys(tmux_exe, session, "F2")
    send_literal(tmux_exe, session, "Z")
    fkey_end = wait_for(tmux_exe, session, r"fkeyZ", "custom F2 cursor-end binding")
    if "fkeyZ" not in fkey_end or "Zfkey" in fkey_end:
        raise RuntimeError(f"F2 custom binding did not move the composer cursor to the end\nscreen:\n{fkey_end}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "insert")
    send_keys(tmux_exe, session, "Insert")
    send_literal(tmux_exe, session, "Z")
    insert_start = wait_for(tmux_exe, session, r"Zinsert", "custom Insert cursor-start binding")
    if "Zinsert" not in insert_start or "insertZ" in insert_start:
        raise RuntimeError(f"Insert custom binding did not move the composer cursor to the start\nscreen:\n{insert_start}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "ctrlone")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1b[27;5;49~")
    send_literal(tmux_exe, session, "Z")
    ctrl_one_end = wait_for(tmux_exe, session, r"ctrloneZ", "custom Ctrl+1 cursor-end binding")
    if "ctrloneZ" not in ctrl_one_end or "Zctrlone" in ctrl_one_end:
        raise RuntimeError(f"Ctrl+1 custom binding did not move the composer cursor to the end\nscreen:\n{ctrl_one_end}")
    send_keys(tmux_exe, session, "C-u")
    send_keys(tmux_exe, session, "M-j")
    session_resume_key = wait_for(tmux_exe, session, r"Select session|Session tree", "custom session resume key")
    if "Select session" not in session_resume_key and "Session tree" not in session_resume_key:
        raise RuntimeError(f"Alt+J custom session resume key did not open selector\nscreen:\n{session_resume_key}")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Select session|Session tree", "custom session resume selector dismissed")
    send_keys(tmux_exe, session, "M-k")
    session_new_key = wait_for(
        tmux_exe,
        session,
        r'(?s)started session "Untitled session" · id.*?session_.*previous session "Untitled session" · id.*?session_.*switched to "Untitled session"',
        "custom session new key",
    )
    assert_title_first_new_receipt(session_new_key, "Untitled session", "Untitled session", "Alt+K custom session new key")
    send_literal(tmux_exe, session, "alt-up-visible")
    send_keys(tmux_exe, session, "M-Up")
    send_literal(tmux_exe, session, "Z")
    alt_up_delivery = wait_for(
        tmux_exe,
        session,
        r"Zalt-up-visible",
        "alt-up key delivery",
    )
    if "Zalt-up-visible" not in alt_up_delivery:
        raise RuntimeError(f"Alt+Up did not reach the TUI keybinding layer\nscreen:\n{alt_up_delivery}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "ctrlh")
    send_keys(tmux_exe, session, "C-h")
    send_literal(tmux_exe, session, "Z")
    ctrl_h_delete = wait_for(tmux_exe, session, r"ctrlZ", "ctrl-h delete backward binding")
    if "ctrlZ" not in ctrl_h_delete or "ctrlhZ" in ctrl_h_delete:
        raise RuntimeError(f"Ctrl+H did not delete the previous composer character\nscreen:\n{ctrl_h_delete}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "alth")
    send_keys(tmux_exe, session, "M-h")
    send_literal(tmux_exe, session, "Z")
    alt_h_left = wait_for(tmux_exe, session, r"altZh", "alt-h cursor-left binding")
    if "altZh" not in alt_h_left or "althZ" in alt_h_left:
        raise RuntimeError(f"Alt+H did not move the composer cursor left\nscreen:\n{alt_h_left}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "alpha beta")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1bw")
    send_literal(tmux_exe, session, "Y")
    alt_w_word = wait_for(tmux_exe, session, r"alphaY beta", "alt-w cursor-word-right binding")
    if "alphaY beta" not in alt_w_word or "Yalpha beta" in alt_w_word:
        raise RuntimeError(f"Alt+W did not move the composer cursor right by word\nscreen:\n{alt_w_word}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "alpha beta")
    click_cursor_draft = wait_for(tmux_exe, session, r"alpha beta", "composer mouse cursor draft")
    click_cursor_row = next(
        ((index + 1, line) for index, line in enumerate(click_cursor_draft.splitlines()) if "alpha beta" in line),
        None,
    )
    if click_cursor_row is None:
        raise RuntimeError(f"composer draft did not expose a clickable row\nscreen:\n{click_cursor_draft}")
    click_cursor_row_number, click_cursor_row_text = click_cursor_row
    click_cursor_column = click_cursor_row_text.index("alpha beta") + len("alpha ") + 1
    send_literal(tmux_exe, session, f"\x1b[<0;{click_cursor_column};{click_cursor_row_number}M")
    send_literal(tmux_exe, session, "Z")
    clicked_cursor = wait_for(tmux_exe, session, r"alpha Zbeta", "raw SGR composer cursor click")
    if "alpha Zbeta" not in clicked_cursor or "alpha betaZ" in clicked_cursor:
        raise RuntimeError(f"raw SGR composer click did not move the draft cursor\nscreen:\n{clicked_cursor}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "drag one")
    drag_cursor_draft = wait_for(tmux_exe, session, r"drag one", "composer mouse selection draft")
    drag_cursor_row = next(
        ((index + 1, line) for index, line in enumerate(drag_cursor_draft.splitlines()) if "drag one" in line),
        None,
    )
    if drag_cursor_row is None:
        raise RuntimeError(f"composer draft did not expose a draggable row\nscreen:\n{drag_cursor_draft}")
    drag_cursor_row_number, drag_cursor_row_text = drag_cursor_row
    drag_anchor_column = drag_cursor_row_text.index("drag one") + len("drag ") + 1
    drag_focus_column = drag_cursor_row_text.index("drag one") + len("drag one") + 1
    send_literal(tmux_exe, session, f"\x1b[<0;{drag_anchor_column};{drag_cursor_row_number}M")
    send_literal(tmux_exe, session, f"\x1b[<32;{drag_focus_column};{drag_cursor_row_number}M")
    send_literal(tmux_exe, session, f"\x1b[<0;{drag_focus_column};{drag_cursor_row_number}m")
    send_literal(tmux_exe, session, "TWO")
    dragged_selection = wait_for(tmux_exe, session, r"drag TWO", "raw SGR composer drag selection replacement")
    if "drag TWO" not in dragged_selection or "drag oneTWO" in dragged_selection:
        raise RuntimeError(f"raw SGR drag/release did not select and replace draft text\nscreen:\n{dragged_selection}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "copy me")
    wait_for(tmux_exe, session, r"copy me", "keyboard copy selection draft")
    cursor_before_selection = pane_cursor_position(tmux_exe, session)
    send_literal(tmux_exe, session, "\x1b[1;2D")
    cursor_after_first_selection = wait_for_cursor_change(
        tmux_exe, session, cursor_before_selection, "first Shift+Left selection"
    )
    send_literal(tmux_exe, session, "\x1b[1;2D")
    wait_for_cursor_change(tmux_exe, session, cursor_after_first_selection, "second Shift+Left selection")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "X")
    keyboard_selection = wait_for(tmux_exe, session, r"copy X", "keyboard selection replacement")
    if "copy X" not in keyboard_selection or "copy meX" in keyboard_selection:
        raise RuntimeError(f"Shift+Arrow selection did not stay replaceable after copy\nscreen:\n{keyboard_selection}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "before external")
    send_keys(tmux_exe, session, "C-g")
    external_editor = wait_for(
        tmux_exe,
        session,
        r"external editor draft|external editor updated draft",
        "Ctrl+G external editor draft replacement",
    )
    if "external editor draft" not in external_editor:
        raise RuntimeError(f"Ctrl+G external editor did not replace the visible draft\nscreen:\n{external_editor}")
    wait_for_pane_command(tmux_exe, session, r"^ava$", "external editor process return")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"external editor draft", "composer clear after external editor")
    send_literal(tmux_exe, session, "erase XY")
    wait_for(tmux_exe, session, r"erase XY", "Backspace selection draft")
    send_literal(tmux_exe, session, "\x1b[1;2D")
    send_literal(tmux_exe, session, "\x1b[1;2D")
    send_keys(tmux_exe, session, "C-h")
    backspace_deleted_selection = wait_for_absent(
        tmux_exe, session, r"erase XY", "Backspace selected composer text deletion"
    )
    if "erase" not in backspace_deleted_selection or "erase XY" in backspace_deleted_selection:
        raise RuntimeError(
            f"Backspace did not delete the selected composer text\nscreen:\n{backspace_deleted_selection}"
        )
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"erase", "composer clear before Delete selection test")
    send_literal(tmux_exe, session, "trim UV")
    wait_for(tmux_exe, session, r"trim UV", "Delete selection draft")
    send_literal(tmux_exe, session, "\x1b[1;2D")
    send_literal(tmux_exe, session, "\x1b[1;2D")
    send_keys(tmux_exe, session, "Delete")
    delete_removed_selection = wait_for_absent(
        tmux_exe, session, r"trim UV", "Delete selected composer text deletion"
    )
    if "trim" not in delete_removed_selection or "trim UV" in delete_removed_selection:
        raise RuntimeError(f"Delete did not delete the selected composer text\nscreen:\n{delete_removed_selection}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "shiftback")
    wait_for(tmux_exe, session, r"shiftback", "Shift+Backspace draft")
    send_literal(tmux_exe, session, "\x1b[127;2u")
    send_literal(tmux_exe, session, "Z")
    shift_backspace = wait_for(tmux_exe, session, r"shiftbacZ", "Shift+Backspace delete-backward alias")
    if "shiftbacZ" not in shift_backspace or "shiftbackZ" in shift_backspace:
        raise RuntimeError(f"Shift+Backspace did not delete the previous composer character\nscreen:\n{shift_backspace}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "shiftdelete")
    wait_for(tmux_exe, session, r"shiftdelete", "Shift+Delete draft")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1b[3$")
    send_literal(tmux_exe, session, "Z")
    shift_delete = wait_for(tmux_exe, session, r"Zhiftdelete", "Shift+Delete delete-forward alias")
    if "Zhiftdelete" not in shift_delete or "Zshiftdelete" in shift_delete:
        raise RuntimeError(f"Shift+Delete did not delete the next composer character\nscreen:\n{shift_delete}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "one two three")
    wait_for(tmux_exe, session, r"one two three", "word selection draft")
    send_literal(tmux_exe, session, "\x1b[1;6D")
    send_literal(tmux_exe, session, "THREE")
    word_selection = wait_for(tmux_exe, session, r"one two THREE", "Shift+Ctrl+Left word selection replacement")
    if "one two THREE" not in word_selection or "one two threeTHREE" in word_selection:
        raise RuntimeError(f"Shift+Ctrl+Left did not select and replace the previous word\nscreen:\n{word_selection}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "line start")
    wait_for(tmux_exe, session, r"line start", "line-start selection draft")
    send_literal(tmux_exe, session, "\x1b[1;2H")
    send_literal(tmux_exe, session, "home")
    line_start_selection = wait_for(tmux_exe, session, r"home", "Shift+Home line-start selection replacement")
    if "home" not in line_start_selection or "line starthome" in line_start_selection:
        raise RuntimeError(f"Shift+Home did not select and replace to the line start\nscreen:\n{line_start_selection}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "end line")
    wait_for(tmux_exe, session, r"end line", "line-end selection draft")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1b[1;2F")
    send_literal(tmux_exe, session, "END")
    line_end_selection = wait_for(tmux_exe, session, r"END", "Shift+End line-end selection replacement")
    if "END" not in line_end_selection or "ENDend line" in line_end_selection or "end lineEND" in line_end_selection:
        raise RuntimeError(f"Shift+End did not select and replace to the line end\nscreen:\n{line_end_selection}")
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "\x1b[200~sxhome alpha\nsxhome beta\x1b[201~")
    wait_for(tmux_exe, session, r"sxhome beta", "document-start selection draft")
    send_literal(tmux_exe, session, "\x1b[1;6H")
    send_literal(tmux_exe, session, "DOCSTART")
    document_start_selection = wait_for(
        tmux_exe, session, r"DOCSTART", "Shift+Ctrl+Home document-start selection replacement"
    )
    if "DOCSTART" not in document_start_selection or "sxhome" in document_start_selection:
        raise RuntimeError(
            "Shift+Ctrl+Home did not select and replace to the document start\n"
            f"screen:\n{document_start_selection}"
        )
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "\x1b[200~sxend alpha\nsxend beta\x1b[201~")
    wait_for(tmux_exe, session, r"sxend beta", "document-end selection draft")
    send_literal(tmux_exe, session, "\x1b[1;5H")
    send_literal(tmux_exe, session, "\x1b[1;6F")
    send_literal(tmux_exe, session, "DOCEND")
    document_end_selection = wait_for(
        tmux_exe, session, r"DOCEND", "Shift+Ctrl+End document-end selection replacement"
    )
    if "DOCEND" not in document_end_selection or "sxend" in document_end_selection:
        raise RuntimeError(
            "Ctrl+Home plus Shift+Ctrl+End did not select and replace to the document end\n"
            f"screen:\n{document_end_selection}"
        )
    send_keys(tmux_exe, session, "C-c")
    send_literal(tmux_exe, session, "\x1b[200~top\nbot\x1b[201~")
    wait_for(tmux_exe, session, r"bot", "Shift+Up selection draft")
    send_literal(tmux_exe, session, "\x1b[1;2A")
    send_literal(tmux_exe, session, "UP")
    shift_up_selection = wait_for(tmux_exe, session, r"topUP", "Shift+Up vertical selection replacement")
    if "topUP" not in shift_up_selection or "botUP" in shift_up_selection:
        raise RuntimeError(f"Shift+Up did not select and replace the previous line span\nscreen:\n{shift_up_selection}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"topUP", "Shift+Up selection draft clear")
    send_literal(tmux_exe, session, "\x1b[200~one\ntwo\x1b[201~")
    send_literal(tmux_exe, session, "\x1b[1;5H")
    send_literal(tmux_exe, session, "\x1b[1;2B")
    send_literal(tmux_exe, session, "DOWN")
    shift_down_selection = wait_for(tmux_exe, session, r"DOWNtwo", "Shift+Down vertical selection replacement")
    if "DOWNtwo" not in shift_down_selection or "oneDOWN" in shift_down_selection:
        raise RuntimeError(f"Shift+Down did not select and replace the next line span\nscreen:\n{shift_down_selection}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "kitty ")
    send_literal(tmux_exe, session, "\x1b[57400u")
    send_literal(tmux_exe, session, " nav")
    kitty_keypad_text = wait_for(tmux_exe, session, r"kitty 1 nav", "Kitty CSI-u keypad printable input")
    if "kitty 1 nav" not in kitty_keypad_text:
        raise RuntimeError(f"Kitty CSI-u keypad printable input did not insert text\nscreen:\n{kitty_keypad_text}")
    send_literal(tmux_exe, session, "\x1b[57417u")
    send_literal(tmux_exe, session, "Z")
    kitty_keypad_left = wait_for(tmux_exe, session, r"kitty 1 naZv", "Kitty CSI-u keypad left navigation")
    if "kitty 1 naZv" not in kitty_keypad_left or "kitty 1 navZ" in kitty_keypad_left:
        raise RuntimeError(f"Kitty CSI-u keypad left did not move the composer cursor\nscreen:\n{kitty_keypad_left}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "neg ")
    send_literal(tmux_exe, session, "\x1b[?0u")
    send_literal(tmux_exe, session, "\x1b[?62;4;52c")
    send_literal(tmux_exe, session, "ok")
    negotiation_text = wait_for(tmux_exe, session, r"neg ok", "keyboard protocol negotiation replies ignored")
    if "neg ok" not in negotiation_text or "?0u" in negotiation_text or "?62;4;52c" in negotiation_text:
        raise RuntimeError(f"keyboard protocol negotiation replies leaked into the draft\nscreen:\n{negotiation_text}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "modify ")
    send_literal(tmux_exe, session, "\x1b[27;1;120~")
    send_literal(tmux_exe, session, "\x1b[27;2;69~")
    send_literal(tmux_exe, session, " key")
    modify_text = wait_for(tmux_exe, session, r"modify xE key", "xterm modifyOtherKeys printable input")
    if "modify xE key" not in modify_text:
        raise RuntimeError(f"xterm modifyOtherKeys printable input did not insert text\nscreen:\n{modify_text}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "mod one")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1b[27;3;100~")
    send_literal(tmux_exe, session, "Z")
    modify_alt_d = wait_for(tmux_exe, session, r"Z one", "xterm modifyOtherKeys Alt+D delete-forward")
    if "Z one" not in modify_alt_d or "Zmod one" in modify_alt_d:
        raise RuntimeError(f"xterm modifyOtherKeys Alt+D did not delete the next word\nscreen:\n{modify_alt_d}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "alpha beta gamma")
    send_keys(tmux_exe, session, "C-w")
    # Cursor motion deliberately breaks backward-kill accumulation so this
    # terminal case exercises yank-pop between two ring entries. Consecutive
    # kill accumulation has deterministic composer coverage.
    send_keys(tmux_exe, session, "Left")
    send_keys(tmux_exe, session, "C-w")
    send_keys(tmux_exe, session, "C-y")
    yanked_text = wait_for(tmux_exe, session, r"alpha beta", "ctrl-y kill-ring yank")
    if "alpha beta" not in yanked_text:
        raise RuntimeError(f"Ctrl+Y did not yank the latest kill-ring entry\nscreen:\n{yanked_text}")
    send_literal(tmux_exe, session, "\x1by")
    yank_pop = wait_for(tmux_exe, session, r"alpha gamma", "alt-y kill-ring yank-pop")
    if "alpha gamma" not in yank_pop:
        raise RuntimeError(f"Alt+Y did not cycle the yanked kill-ring entry\nscreen:\n{yank_pop}")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "ctrl-enter-one")
    send_literal(tmux_exe, session, "\x1b[13;5u")
    send_literal(tmux_exe, session, "tail")
    modified_enter = wait_for(
        tmux_exe,
        session,
        r"ctrl-enter-one[^\n]*\n[^\n]*tail",
        "Ctrl+Enter newline alias",
    )
    if "ctrl-enter-onetail" in modified_enter:
        raise RuntimeError(f"Ctrl+Enter did not create a multiline draft break\nscreen:\n{modified_enter}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"ctrl-enter-one|tail", "Ctrl+Enter draft clear")

    send_literal(tmux_exe, session, "slash-newline\\")
    send_keys(tmux_exe, session, "Enter")
    send_literal(tmux_exe, session, "tail")
    backslash_enter = wait_for(
        tmux_exe,
        session,
        r"slash-newline[^\n]*\n[^\n]*tail",
        "backslash Enter newline workaround",
    )
    if "slash-newlinetail" in backslash_enter:
        raise RuntimeError(f"Backslash+Enter did not create a multiline draft break\nscreen:\n{backslash_enter}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"slash-newline|tail", "backslash Enter draft clear")

    send_literal(tmux_exe, session, "/help")
    wait_for(tmux_exe, session, r"/help", "Alt+Enter idle submit draft")
    send_literal(tmux_exe, session, "\x1b\r")
    alt_enter_help = wait_for(
        tmux_exe,
        session,
        r"page_up PageUp|model_cycle_forward|details_toggle|tree_fold_or_up|tree_unfold_or_down",
        "Alt+Enter idle submit help output",
    )
    if (
        "page_up PageUp" not in alt_enter_help
        and "model_cycle_forward" not in alt_enter_help
        and "details_toggle" not in alt_enter_help
        and "tree_fold_or_up" not in alt_enter_help
        and "tree_unfold_or_down" not in alt_enter_help
    ):
        raise RuntimeError(f"Alt+Enter did not submit the /help command while idle\nscreen:\n{alt_enter_help}")
    send_keys(tmux_exe, session, "C-u")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/keybindings")
    wait_for(tmux_exe, session, r"/keybindings", "keybindings alias draft")
    send_keys(tmux_exe, session, "Enter")
    keybindings_modal = wait_for(tmux_exe, session, r"Keybindings|keybindings opened", "keybindings alias modal")
    if "Keybindings" not in keybindings_modal:
        raise RuntimeError(f"/keybindings did not open the keybinding discovery modal\nscreen:\n{keybindings_modal}")
    send_keys(tmux_exe, session, "C-w")
    wait_for_absent(tmux_exe, session, r"Search keybindings", "keybindings modal canceled by custom Ctrl+W")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/keybindings")
    wait_for(tmux_exe, session, r"/keybindings", "keybindings alias draft for Space confirm")
    send_keys(tmux_exe, session, "Enter")
    keybindings_modal = wait_for(tmux_exe, session, r"Keybindings|keybindings opened", "keybindings alias modal for Space confirm")
    if "Keybindings" not in keybindings_modal:
        raise RuntimeError(f"/keybindings did not reopen the keybinding discovery modal\nscreen:\n{keybindings_modal}")
    send_keys(tmux_exe, session, "Space")
    wait_for_absent(tmux_exe, session, r"Search keybindings", "keybindings modal selected by custom Space")

    (ava_config / "keybinds.json").write_text(
        '{"tui.editor.cursorLineStart":["Home","Ctrl+A","Alt+Up"],'
        '"tui.editor.cursorLeft":["Left","Ctrl+B","Alt+H"],'
        '"tui.editor.cursorWordRight":["Ctrl+Right","Alt+Right","Alt+F","Alt+W"],'
        '"tui.editor.deleteCharBackward":["Ctrl+H"]}\n',
        encoding="utf-8",
    )
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/reload")
    wait_for(tmux_exe, session, r"Reload config domains", "restore default select bindings reload description")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Reload config domains", "restore default select bindings reload description dismissed")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"keybindings reloaded", "default select bindings restored")

    _finish_main(tmux_exe, session)
