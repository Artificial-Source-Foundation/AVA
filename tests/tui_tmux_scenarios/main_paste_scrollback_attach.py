"""The tmux TUI smoke scenario for main paste scrollback attach."""

from __future__ import annotations

from tui_smoke_helpers import (
    SmokeContext,
    save_evidence,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_absent,
    wait_for_session_exit,
    wait_for_screen_state,
)
from .common import _main_session


def scenario_main_paste_scrollback_attach(ctx: SmokeContext) -> None:
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)
    large_paste = "\n".join(f"line{i:02d}" for i in range(1, 12))
    send_literal(tmux_exe, session, f"\x1b[200~{large_paste}\x1b[201~")
    paste_marker = wait_for(tmux_exe, session, r"\[paste #1 \+11 lines\]", "large bracketed paste marker")
    if "line11" in paste_marker:
        raise RuntimeError(f"large paste content leaked instead of collapsing to a marker\nscreen:\n{paste_marker}")
    save_evidence(root, "large-paste-marker", paste_marker)
    send_keys(tmux_exe, session, "Left")
    send_literal(tmux_exe, session, "X")
    atomic_marker = wait_for(tmux_exe, session, r"X\[paste #1 \+11 lines\]", "large paste marker atomic left movement")
    if "linesX" in atomic_marker:
        raise RuntimeError(f"left-arrow entered the paste marker instead of jumping over it\nscreen:\n{atomic_marker}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"X\[paste #1 \+11 lines\]", "large paste marker clear")

    send_literal(tmux_exe, session, "A")
    send_literal(tmux_exe, session, f"\x1b[200~{large_paste}\x1b[201~")
    send_literal(tmux_exe, session, "B")
    wait_for(tmux_exe, session, r"A\[paste #1 \+11 lines\]B", "large paste marker forward delete draft")
    send_keys(tmux_exe, session, "C-a", "Right", "Delete")
    wait_for(tmux_exe, session, r"│  AB|^AB$", "large paste marker atomic forward delete")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"A\[paste #1 \+11 lines\]B|│  AB", "large paste marker forward delete clear")

    send_literal(tmux_exe, session, "X ")
    send_literal(tmux_exe, session, f"\x1b[200~{large_paste}\x1b[201~")
    send_literal(tmux_exe, session, " Y")
    wait_for(tmux_exe, session, r"X \[paste #1 \+11 lines\] Y", "large paste marker word draft")
    send_keys(tmux_exe, session, "C-a")
    send_keys(tmux_exe, session, "M-f", "M-f")
    send_literal(tmux_exe, session, "Z")
    wait_for(tmux_exe, session, r"X \[paste #1 \+11 lines\]Z Y", "large paste marker atomic word movement")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"X \[paste #1 \+11 lines\]Z Y", "large paste marker word draft clear")

    send_literal(tmux_exe, session, "alpha beta")
    send_literal(tmux_exe, session, "\x1b[1;3D")
    send_literal(tmux_exe, session, "Z")
    alt_left_word = wait_for(tmux_exe, session, r"alpha Zbeta", "alt-left word movement")
    if "alpha betaZ" in alt_left_word:
        raise RuntimeError(f"Alt+Left did not move to the previous word before insertion\nscreen:\n{alt_left_word}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"alpha Zbeta", "alt-left word movement clear")

    send_literal(tmux_exe, session, "alpha beta")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1b[1;3C")
    send_literal(tmux_exe, session, "Y")
    alt_right_word = wait_for(tmux_exe, session, r"alphaY beta", "alt-right word movement")
    if "Yalpha beta" in alt_right_word:
        raise RuntimeError(f"Alt+Right did not move to the next word before insertion\nscreen:\n{alt_right_word}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"alphaY beta", "alt-right word movement clear")

    send_literal(tmux_exe, session, "path/to/file")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1b[1;3C")
    send_literal(tmux_exe, session, "Z")
    punctuation_word = wait_for(tmux_exe, session, r"pathZ/to/file", "alt-right punctuation word boundary")
    if "path/to/fileZ" in punctuation_word:
        raise RuntimeError(
            f"Alt+Right skipped the path punctuation boundary instead of stopping after the first segment\nscreen:\n{punctuation_word}"
        )
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"pathZ/to/file", "alt-right punctuation word boundary clear")

    send_literal(tmux_exe, session, "one two three")
    send_keys(tmux_exe, session, "C-a", "M-f", "M-d")
    forward_word_delete = wait_for(tmux_exe, session, r"│  one three", "alt-d forward word deletion")
    if "one two three" in forward_word_delete:
        raise RuntimeError(f"Alt+D did not delete the next word\nscreen:\n{forward_word_delete}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"one three", "alt-d forward word deletion clear")

    send_literal(tmux_exe, session, "alpha beta gamma")
    send_keys(tmux_exe, session, "C-a", "M-f")
    send_literal(tmux_exe, session, "\x1b[3;3~")
    alt_delete = wait_for(tmux_exe, session, r"alpha gamma", "alt-delete forward word deletion")
    if "alpha beta gamma" in alt_delete:
        raise RuntimeError(f"Alt+Delete did not delete the next word\nscreen:\n{alt_delete}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"alpha gamma", "alt-delete forward word deletion clear")

    send_literal(tmux_exe, session, "left eraseword")
    send_literal(tmux_exe, session, "\x1b\x7f")
    send_literal(tmux_exe, session, "Z")
    alt_backspace = wait_for(tmux_exe, session, r"left Z", "alt-backspace backward word deletion")
    if "eraseword" in alt_backspace:
        raise RuntimeError(f"Alt+Backspace did not delete the previous word\nscreen:\n{alt_backspace}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"left Z", "alt-backspace backward word deletion clear")

    send_literal(tmux_exe, session, "abcXdef")
    send_keys(tmux_exe, session, "C-a", "Right", "Right", "Right", "C-d")
    ctrl_d_delete = wait_for(tmux_exe, session, r"abcdef", "ctrl-d forward character deletion")
    if "abcXdef" in ctrl_d_delete:
        raise RuntimeError(f"Ctrl+D did not delete the next character\nscreen:\n{ctrl_d_delete}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"abcdef", "ctrl-d forward deletion draft clear")

    send_literal(tmux_exe, session, "hello world")
    send_keys(tmux_exe, session, "C-a")
    send_literal(tmux_exe, session, "\x1d")
    send_literal(tmux_exe, session, "o")
    send_literal(tmux_exe, session, "Y")
    jump_forward = wait_for(tmux_exe, session, r"hellYo world", "ctrl-bracket jump forward")
    if "Yhello world" in jump_forward:
        raise RuntimeError(f"Ctrl+] inserted instead of jumping forward\nscreen:\n{jump_forward}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"hellYo world", "ctrl-bracket jump-forward draft clear")

    send_literal(tmux_exe, session, "alpha beta gamma")
    send_literal(tmux_exe, session, "\x1b\x1d")
    send_literal(tmux_exe, session, "b")
    send_literal(tmux_exe, session, "Z")
    jump_backward = wait_for(tmux_exe, session, r"alpha Zbeta gamma", "ctrl-alt-bracket jump backward")
    if "alpha beta gammaZ" in jump_backward:
        raise RuntimeError(f"Ctrl+Alt+] inserted instead of jumping backward\nscreen:\n{jump_backward}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"alpha Zbeta gamma", "ctrl-alt-bracket jump-backward draft clear")

    send_literal(tmux_exe, session, "undo word")
    send_keys(tmux_exe, session, "C-w")
    wait_for(tmux_exe, session, r"│  undo", "ctrl-w draft before ctrl-minus undo")
    send_literal(tmux_exe, session, "\x1f")
    wait_for(tmux_exe, session, r"undo word", "ctrl-minus undo restores draft")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"undo word", "ctrl-minus undo draft clear")

    scroll_seed = "paste scenario transcript seed\n" + "\n".join(
        f"PASTE SCROLLBACK LINE {index:02d}" for index in range(1, 31)
    )
    send_literal(tmux_exe, session, f"\x1b[200~{scroll_seed}\x1b[201~")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"PASTE SCROLLBACK LINE 30", "ordinary multiline scrollback seed output")
    send_literal(tmux_exe, session, "\x1b[200~first\nsecond\x1b[201~")
    multiline_live = wait_for(tmux_exe, session, r"second", "multiline draft before transcript scroll")
    multiline_transcript_rows = tuple(multiline_live.splitlines()[:-4])
    send_literal(tmux_exe, session, "\x1b[1;129A")
    multiline_scrolled = wait_for_screen_state(
        tmux_exe,
        session,
        lambda screen: tuple(screen.splitlines()[:-4]) != multiline_transcript_rows and "first" in screen and "second" in screen,
        "multiline draft physical Ghostty arrow transcript movement",
    )
    # /help intentionally lists the jump_to_bottom action name; only the
    # removed detached-banner phrases indicate stale product chrome.
    if any(text in multiline_scrolled for text in ("scrollback detached", "updates below")):
        raise RuntimeError(f"multiline arrow scroll surfaced deleted detached chrome\nscreen:\n{multiline_scrolled}")
    send_literal(tmux_exe, session, "X")
    moved = wait_for(tmux_exe, session, r"secondX", "multiline draft cursor preserved by arrow scroll")
    if "firstX" in moved:
        raise RuntimeError(f"arrow-up moved the multiline composer cursor instead of scrolling only the transcript\nscreen:\n{moved}")
    send_literal(tmux_exe, session, "\x1b[1;129B")
    wait_for_screen_state(
        tmux_exe,
        session,
        lambda screen: tuple(screen.splitlines()[:-4]) == multiline_transcript_rows and "secondX" in screen,
        "multiline draft return to live tail",
    )
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"secondX", "multiline transcript-scroll draft clear")

    send_literal(tmux_exe, session, "\x1b[200~home\nend\x1b[201~")
    wait_for(tmux_exe, session, r"home.*end|home", "multiline draft before home/end cursor movement")
    send_keys(tmux_exe, session, "Home")
    send_literal(tmux_exe, session, "S")
    send_keys(tmux_exe, session, "End")
    send_literal(tmux_exe, session, "E")
    home_end = wait_for(tmux_exe, session, r"SendE", "home/end line-boundary cursor movement")
    if "homeS" in home_end:
        raise RuntimeError(f"Home edited the previous line instead of current line start\nscreen:\n{home_end}")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"SendE", "home/end draft clear")

    send_literal(tmux_exe, session, "\x1b[200~join\nline\x1b[201~")
    wait_for(tmux_exe, session, r"join.*line|join", "multiline draft before ctrl-k line join")
    send_literal(tmux_exe, session, "\x1b[1;133H")
    send_keys(tmux_exe, session, "End", "C-k")
    wait_for(tmux_exe, session, r"joinline", "ctrl-home/end ctrl-k line join")
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"joinline", "ctrl-k line-join draft clear")

    send_literal(tmux_exe, session, "\x1b[200~alpha\nbeta\x1b[201~")
    pasted = wait_for(tmux_exe, session, r"pasted into draft safely|alpha", "bracketed paste handling")
    if "[200~" in pasted or "[201~" in pasted:
        raise RuntimeError(f"bracketed paste markers leaked into the visible screen\nscreen:\n{pasted}")

    tmux(tmux_exe, "resize-window", "-t", session, "-x", "82", "-y", "20")
    resized = wait_for(tmux_exe, session, r"alpha|Type a message|pasted into draft safely", "resize redraw")
    if "Traceback" in resized or "assert" in resized.lower():
        raise RuntimeError(f"resize frame shows failure text\nscreen:\n{resized}")
    save_evidence(root, "resize-redraw", resized)

    send_keys(tmux_exe, session, "C-c")
    if tmux(tmux_exe, "has-session", "-t", session, check=False).returncode != 0:
        raise RuntimeError("AVA exited while clearing the bracketed-paste draft")
    wait_for_absent(tmux_exe, session, r"alpha|beta", "draft clear before quit")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "draft stays while scrolling")
    transcript_live = wait_for(tmux_exe, session, r"draft stays while scrolling", "draft before transcript-only scrolling")
    transcript_live_rows = tuple(transcript_live.splitlines()[:-3])
    send_literal(tmux_exe, session, "\x1b[<64;4;6M")
    wheel_scrolled = wait_for_screen_state(
        tmux_exe,
        session,
        lambda screen: tuple(screen.splitlines()[:-3]) != transcript_live_rows and "draft stays while scrolling" in screen,
        "raw SGR mouse wheel transcript movement",
    )
    if any(text in wheel_scrolled for text in ("scrollback detached", "updates below")):
        raise RuntimeError(f"raw SGR mouse wheel surfaced deleted detached chrome\nscreen:\n{wheel_scrolled}")
    # Reset the physical-wheel burst at a harmless non-wheel boundary before
    # asserting the reverse gesture; Right at end-of-draft preserves content.
    send_literal(tmux_exe, session, "\x1b[1;129C\x1b[<65;4;6M")
    wheel_tail = wait_for_screen_state(
        tmux_exe,
        session,
        lambda screen: tuple(screen.splitlines()[:-3]) == transcript_live_rows and "draft stays while scrolling" in screen,
        "raw SGR mouse wheel return to live tail",
    )
    send_literal(tmux_exe, session, "\x1b[1;129A")
    arrow_scrolled = wait_for_screen_state(
        tmux_exe,
        session,
        lambda screen: tuple(screen.splitlines()[:-3]) != transcript_live_rows and "draft stays while scrolling" in screen,
        "physical Ghostty Up arrow transcript movement with Num Lock",
    )
    if any(text in arrow_scrolled for text in ("scrollback detached", "updates below")):
        raise RuntimeError(f"physical Up arrow surfaced deleted detached chrome\nscreen:\n{arrow_scrolled}")
    send_literal(tmux_exe, session, "\x1b[1;129B")
    arrow_tail = wait_for_screen_state(
        tmux_exe,
        session,
        lambda screen: tuple(screen.splitlines()[:-3]) == transcript_live_rows and "draft stays while scrolling" in screen,
        "physical Ghostty Down arrow return to live tail with Num Lock",
    )
    send_keys(tmux_exe, session, "C-c")
    wait_for_absent(tmux_exe, session, r"draft stays while scrolling", "scrolling regression draft clear")

    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/att")
    attach_palette = wait_for(tmux_exe, session, r"/attach.*Attach an image", "attach command palette")
    if "/attach" not in attach_palette or "Attach an image" not in attach_palette:
        raise RuntimeError(f"/attach did not appear in the slash palette\nscreen:\n{attach_palette}")
    save_evidence(root, "slash-attach-palette", attach_palette)
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Attach an image", "attach palette dismissed")
    send_keys(tmux_exe, session, "C-u")
    send_literal(tmux_exe, session, "/attach screen.png")
    wait_for(tmux_exe, session, r"/attach screen\.png", "attach image draft")
    send_keys(tmux_exe, session, "Enter")
    attached_image = wait_for(
        tmux_exe, session, r"attached image.*screen\.png|screen\.png.*next prompt", "attached image pending row"
    )
    if (
        "attached image" not in attached_image
        or "screen.png" not in attached_image
        or "preview text-only" not in attached_image
        or "next prompt" not in attached_image
    ):
        raise RuntimeError(f"/attach did not import and queue the image visibly\nscreen:\n{attached_image}")
    save_evidence(root, "attachment-text-fallback", attached_image)
    send_keys(tmux_exe, session, "C-v")
    clipboard_image = wait_for(
        tmux_exe,
        session,
        r"attached image clipboard image.*next pr|pasted clipboard image for next prompt",
        "Ctrl+V clipboard image pending row",
    )
    if (
        "attached image clipboard image" not in clipboard_image
        or "clipboard image" not in clipboard_image
        or "preview text-only" not in clipboard_image
        or "next prompt" not in clipboard_image
    ):
        raise RuntimeError(f"Ctrl+V did not import and queue the clipboard image visibly\nscreen:\n{clipboard_image}")

    send_keys(tmux_exe, session, "C-d")
    wait_for_session_exit(tmux_exe, session)
