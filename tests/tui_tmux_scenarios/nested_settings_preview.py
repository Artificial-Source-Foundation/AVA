"""Tmux coverage for nested settings navigation and reversible display preview."""

from __future__ import annotations

import re
import time

from tui_smoke_helpers import (
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
from .common import clear_settings_filter, close_settings, open_settings_root, open_settings_section


def _assert_no_settings_chat_receipt(screen: str, label: str, *receipt_patterns: str) -> None:
    # Successful local settings actions add zero chat/transcript items. Administrative
    # confirmation text may still appear on the transient status dock, whose rows start
    # with the status glyphs (✓/!) at column 0; transcript conversation rows carry the
    # two-cell canvas inset or assistant markers instead. Any receipt match outside the
    # status dock proves an administrative confirmation was styled as chat.
    for pattern in receipt_patterns:
        for line in screen.splitlines():
            if re.search(pattern, line) and not re.match(r"^[✓!]", line):
                raise RuntimeError(
                    f"{label}: settings confirmation {pattern!r} leaked into chat\nscreen:\n{screen}"
                )


def scenario_nested_settings_preview(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    root = ctx.root
    workspace = ctx.workspace
    ava_config = ctx.ava_config
    display_config = ava_config / "display.json"
    session = ctx.session_name("nested-settings")
    env_prefix = ctx.pane_command(
        home=ctx.home,
        config=ctx.config,
        state=ctx.state,
        data=ctx.data,
        extra={"NO_COLOR": "", "AVA_TUI_THEME": "", "COLORFGBG": ""},
    )
    (ava_config / "keybinds.json").write_text(
        '{"tui.editor.cursorLineStart":["Home","Ctrl+A"]}\n', encoding="utf-8"
    )
    if display_config.exists():
        display_config.unlink()

    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        session,
        "-x",
        "100",
        "-y",
        "24",
        "-c",
        str(workspace),
        env_prefix,
    )
    wait_for(tmux_exe, session, r"Type a message|live session", "nested-settings initial TUI frame")

    root_modal = open_settings_root(tmux_exe, session, "nested settings root")
    if "Display" not in root_modal or "Model" not in root_modal:
        raise RuntimeError(f"settings root did not expose shallow sections\nscreen:\n{root_modal}")
    save_evidence(root, "nested-settings-root", root_modal)

    # Section-local filter: root filter must not surface nested Theme rows.
    send_literal(tmux_exe, session, "light")
    root_theme_filter = wait_for(tmux_exe, session, r"Search\s{2}light|No settings match", "root theme filter")
    if "Light" in root_theme_filter and "highlight previews" in root_theme_filter:
        raise RuntimeError(f"root settings filter leaked nested theme rows\nscreen:\n{root_theme_filter}")
    clear_settings_filter(tmux_exe, session, "root theme filter cleared")
    wait_for(tmux_exe, session, r"Theme|Display|Model", "root sections restored after filter clear")

    send_literal(tmux_exe, session, "Display")
    wait_for(tmux_exe, session, r"Search\s{2}Display", "display section filter")
    send_keys(tmux_exe, session, "Enter")
    display_modal = wait_for(tmux_exe, session, r"Settings › Display|Images on|Width 60", "display section opened")
    save_evidence(root, "nested-settings-display", display_modal)

    # Back-stack restores the root frame (including the prior root filter).
    send_keys(tmux_exe, session, "Escape")
    back_to_root = wait_for(tmux_exe, session, r"Theme|Model|Search\s{2}Display", "esc returns to settings root")
    if "Settings › Display" in back_to_root:
        raise RuntimeError(f"Esc from display did not restore settings root\nscreen:\n{back_to_root}")
    clear_settings_filter(tmux_exe, session, "root filter cleared after display back-stack")

    # Short-height nested navigation keeps the selected section visible.
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "72", "-y", "10")
    short_root = wait_for(tmux_exe, session, r"Settings|Display", "short-height settings root")
    send_literal(tmux_exe, session, "Tools")
    short_tools = wait_for(tmux_exe, session, r"Search\s{2}(?:T?ools)", "short-height tools filter")
    if "Tools" not in short_tools:
        raise RuntimeError(f"short-height settings root hid the filtered Tools section\nscreen:\n{short_tools}")
    send_keys(tmux_exe, session, "Enter")
    short_tools_section = wait_for(tmux_exe, session, r"Settings › Tools|Permissions|Plugins", "short-height tools section")
    save_evidence(root, "nested-settings-short-tools", short_tools_section)
    send_keys(tmux_exe, session, "Escape")
    wait_for(tmux_exe, session, r"Theme|Display|Model", "short-height back to root")
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "100", "-y", "24")
    wait_for(tmux_exe, session, r"Theme|Display|Model", "restored normal-height settings root")
    close_settings(tmux_exe, session, "closed before preview checks")
    send_keys(tmux_exe, session, "C-u")

    # Theme highlight previews without writing display.json; Esc restores authority.
    open_settings_section(
        tmux_exe,
        session,
        "Theme",
        r"Settings › Theme|Dark|Light|Plain|sunrise",
        "display section before theme preview cancel",
    )
    before_preview = display_config.read_text(encoding="utf-8") if display_config.exists() else ""
    send_literal(tmux_exe, session, "light")
    preview_row = wait_for(tmux_exe, session, r"Search\s{2}light", "theme light highlight row")
    if display_config.exists() and display_config.read_text(encoding="utf-8") != before_preview:
        raise RuntimeError("theme highlight wrote display.json before confirmation")
    save_evidence(root, "nested-settings-theme-preview", preview_row)
    send_keys(tmux_exe, session, "Escape")
    wait_for(tmux_exe, session, r"Theme|Display|Model", "esc cancels display preview to root")
    if display_config.exists() and display_config.read_text(encoding="utf-8") != before_preview:
        raise RuntimeError("cancel path wrote display.json")
    close_settings(tmux_exe, session, "closed after preview cancel")
    send_keys(tmux_exe, session, "C-u")

    # Confirm writes exactly once and survives restart.
    open_settings_section(
        tmux_exe,
        session,
        "Theme",
        r"Settings › Theme|Dark|Light|Plain|sunrise",
        "display section before theme confirm",
    )
    send_literal(tmux_exe, session, "light")
    wait_for(tmux_exe, session, r"Search\s{2}light", "theme light confirm row")
    send_keys(tmux_exe, session, "Enter")
    # Confirm persists exactly once; synchronize on the refreshed current-row/config state
    # instead of a chat receipt, then prove the administrative confirmation text never
    # reached the chat surface.
    confirmed = wait_for(tmux_exe, session, r"Light[^\n]*current", "theme confirm current row")
    if not display_config.exists() or '"theme": "light"' not in display_config.read_text(encoding="utf-8"):
        raise RuntimeError(f"theme confirm did not write display.json\npath:\n{display_config}")
    if "Stored TUI theme" in confirmed:
        raise RuntimeError(f"theme confirm receipt leaked onto the settings frame\nscreen:\n{confirmed}")
    save_evidence(root, "nested-settings-theme-confirm", confirmed)
    close_settings(tmux_exe, session, "closed after theme confirm")
    send_keys(tmux_exe, session, "C-u")
    after_theme_confirm = wait_for(tmux_exe, session, r"Type a message", "main frame after theme confirm")
    _assert_no_settings_chat_receipt(after_theme_confirm, "theme confirm", r"Stored TUI theme")
    save_evidence(root, "nested-settings-theme-confirm-no-chat-receipt", after_theme_confirm)
    tmux(tmux_exe, "kill-session", "-t", session, check=False)

    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        session,
        "-x",
        "100",
        "-y",
        "24",
        "-c",
        str(workspace),
        env_prefix,
    )
    wait_for(tmux_exe, session, r"Type a message|live session", "nested-settings restart after confirm")
    restarted = open_settings_section(
        tmux_exe,
        session,
        "Theme",
        r"Settings › Theme|Dark|Light|Plain|sunrise",
        "display section after restart",
    )
    if "Light" not in restarted or "current" not in restarted:
        raise RuntimeError(f"restart did not keep confirmed light theme\nscreen:\n{restarted}")
    save_evidence(root, "nested-settings-theme-restart", restarted)

    # External edit during an active preview rebases authority then keeps the staged highlight.
    send_literal(tmux_exe, session, "plain")
    wait_for(tmux_exe, session, r"Search\s{2}plain", "plain theme staged during external edit")
    before_external = display_config.read_text(encoding="utf-8")
    display_config.write_text('{\n  "theme": "dark",\n  "show_images": true,\n  "image_width_cells": 60\n}\n', encoding="utf-8")
    # The Display filter may still be "Theme plain", so dark "current" rows are hidden.
    # Synchronize on the watched file and a live settings frame instead of the covered status dock.
    deadline = time.monotonic() + 8.0
    rebased = capture(tmux_exe, session)
    while time.monotonic() < deadline:
        rebased = capture(tmux_exe, session)
        disk = display_config.read_text(encoding="utf-8")
        settings_still_open = "Settings › Theme" in rebased
        if '"theme": "dark"' in disk and settings_still_open:
            break
        time.sleep(0.05)
    disk = display_config.read_text(encoding="utf-8")
    if '"theme": "dark"' not in disk:
        raise RuntimeError(f"external display.json edit was not observed\npath text:\n{disk}\nscreen:\n{rebased}")
    if '"theme": "plain"' in disk:
        raise RuntimeError("external reload path persisted the staged plain preview")
    if "Settings › Theme" not in rebased:
        raise RuntimeError(f"external reload discarded the settings display frame\nscreen:\n{rebased}")
    save_evidence(root, "nested-settings-external-during-preview", rebased)
    send_keys(tmux_exe, session, "Escape")
    wait_for(tmux_exe, session, r"Theme|Display|Model", "esc after external rebase")
    close_settings(tmux_exe, session, "closed after external rebase")
    send_keys(tmux_exe, session, "C-u")

    # W2-002: while configured theme is dark, a valid edit to an unconfigured previewed custom
    # theme must reload through the catalog watch and refresh the staged overlay.
    themes_dir = ava_config / "themes"
    themes_dir.mkdir(parents=True, exist_ok=True)
    sunrise_path = themes_dir / "sunrise.json"

    def write_sunrise(composer_bg: int) -> None:
        sunrise_path.write_text(
            "{\n"
            '  "name": "sunrise",\n'
            '  "vars": {"primary": "#0066cc", "paper": 255},\n'
            '  "colors": {\n'
            '    "text": "",\n'
            '    "muted": 242,\n'
            '    "success": 34,\n'
            '    "warning": "#ffaa00",\n'
            '    "error": "#ff0000",\n'
            '    "accent": "primary",\n'
            '    "screenBg": "paper",\n'
            f'    "composerBg": {composer_bg},\n'
            '    "toolBg": 235,\n'
            '    "questionBg": 234\n'
            "  }\n"
            "}\n",
            encoding="utf-8",
        )

    write_sunrise(236)
    # Ensure configured authority stays dark (unconfigured sunrise is only a preview candidate).
    display_config.write_text(
        '{\n  "theme": "dark",\n  "show_images": true,\n  "image_width_cells": 60\n}\n',
        encoding="utf-8",
    )
    # Give the display watcher a beat to observe dark authority before opening settings.
    time.sleep(0.6)
    open_settings_section(
        tmux_exe,
        session,
        "Theme",
        r"Settings › Theme|Dark|Light|Plain|sunrise",
        "display section before unconfigured custom theme preview",
    )
    send_literal(tmux_exe, session, "sunrise")
    sunrise_row = wait_for(tmux_exe, session, r"Search\s{2}sunrise|sunrise", "sunrise custom theme staged")
    if "sunrise" not in sunrise_row.lower():
        raise RuntimeError(f"sunrise custom theme was not listed for preview\nscreen:\n{sunrise_row}")
    before_sunrise_edit = display_config.read_text(encoding="utf-8")
    write_sunrise(238)
    deadline = time.monotonic() + 8.0
    sunrise_reloaded = capture(tmux_exe, session)
    while time.monotonic() < deadline:
        sunrise_reloaded = capture(tmux_exe, session)
        disk = display_config.read_text(encoding="utf-8")
        settings_still_open = "Settings › Theme" in sunrise_reloaded
        if settings_still_open and disk == before_sunrise_edit:
            # Catalog-only reload must not persist the staged preview.
            break
        time.sleep(0.05)
    if display_config.read_text(encoding="utf-8") != before_sunrise_edit:
        raise RuntimeError("unconfigured custom theme edit wrote display.json during preview")
    if "Settings › Theme" not in sunrise_reloaded:
        raise RuntimeError(
            f"custom theme catalog reload discarded the settings display frame\nscreen:\n{sunrise_reloaded}"
        )
    save_evidence(root, "nested-settings-custom-theme-catalog-reload", sunrise_reloaded)

    # Invalidating the previewed custom theme must keep the session open and must not persist sunrise.
    sunrise_path.write_text(
        '{\n  "name": "sunrise",\n  "colors": {"text":"","muted":242,"success":34,"warning":220,"error":196,"accent":39,"screenBg":235}\n}\n',
        encoding="utf-8",
    )
    deadline = time.monotonic() + 8.0
    sunrise_invalid = capture(tmux_exe, session)
    while time.monotonic() < deadline:
        sunrise_invalid = capture(tmux_exe, session)
        disk = display_config.read_text(encoding="utf-8")
        settings_still_open = "Settings › Theme" in sunrise_invalid
        if settings_still_open and '"theme": "dark"' in disk and '"theme": "sunrise"' not in disk:
            break
        time.sleep(0.05)
    disk = display_config.read_text(encoding="utf-8")
    if '"theme": "sunrise"' in disk:
        raise RuntimeError("invalid custom theme edit/confirm path persisted sunrise into display.json")
    if '"theme": "dark"' not in disk:
        raise RuntimeError(f"invalid custom theme path disturbed configured dark theme\npath text:\n{disk}")
    save_evidence(root, "nested-settings-custom-theme-invalid-retained", sunrise_invalid)
    close_settings(tmux_exe, session, "closed after custom theme catalog reload")
    send_keys(tmux_exe, session, "C-u")
    # Restore a valid sunrise file so later image checks are unaffected.
    write_sunrise(236)

    # Image visibility/width preview and persistence.
    open_settings_section(
        tmux_exe,
        session,
        "Display",
        r"Settings › Display|Images off|Width 60",
        "display section before image controls",
    )
    image_before = display_config.read_text(encoding="utf-8")
    send_literal(tmux_exe, session, "Images off")
    images_off_row = wait_for(tmux_exe, session, r"Search\s{2}Images off", "images off highlight")
    if display_config.read_text(encoding="utf-8") != image_before:
        raise RuntimeError("images highlight wrote display.json before confirmation")
    save_evidence(root, "nested-settings-images-preview", images_off_row)
    send_keys(tmux_exe, session, "Enter")
    images_off_saved = wait_for(tmux_exe, session, r"Images off[^\n]*current", "images off confirm current row")
    if '"show_images": false' not in display_config.read_text(encoding="utf-8"):
        raise RuntimeError(f"images off confirm did not persist show_images=false\nscreen:\n{images_off_saved}")
    if "Stored TUI image visibility" in images_off_saved:
        raise RuntimeError(f"images off receipt leaked onto the settings frame\nscreen:\n{images_off_saved}")

    clear_settings_filter(tmux_exe, session, "cleared images filter before width")
    send_literal(tmux_exe, session, "Width 80")
    width_row = wait_for(tmux_exe, session, r"Search\s{2}Width 80", "image width highlight")
    width_before_confirm = display_config.read_text(encoding="utf-8")
    if '"image_width_cells": 80' in width_before_confirm:
        raise RuntimeError("image width highlight persisted before confirmation")
    save_evidence(root, "nested-settings-width-preview", width_row)
    send_keys(tmux_exe, session, "Enter")
    width_saved = wait_for(tmux_exe, session, r"Width 80[^\n]*current", "image width confirm current row")
    if '"image_width_cells": 80' not in display_config.read_text(encoding="utf-8"):
        raise RuntimeError(f"image width confirm did not persist width 80\nscreen:\n{width_saved}")
    if "Stored TUI image width" in width_saved:
        raise RuntimeError(f"image width receipt leaked onto the settings frame\nscreen:\n{width_saved}")
    save_evidence(root, "nested-settings-images-width-confirm", width_saved)

    # Mouse selection in nested theme stays non-persisting.
    close_settings(tmux_exe, session, "closed display settings before mouse theme")
    send_keys(tmux_exe, session, "C-u")
    open_settings_section(tmux_exe, session, "Theme", r"Settings › Theme|Dark|Light", "theme rows before mouse selection")
    send_literal(tmux_exe, session, "theme")
    mouse_rows = wait_for(tmux_exe, session, r"Search\s{2}theme", "theme rows for mouse select")
    dark_row = next(
        ((index + 1, line) for index, line in enumerate(mouse_rows.splitlines()) if "Dark" in line),
        None,
    )
    light_row = next(
        ((index + 1, line) for index, line in enumerate(mouse_rows.splitlines()) if "Light" in line),
        None,
    )
    if dark_row is None or light_row is None:
        raise RuntimeError(f"could not locate Dark/Light rows for mouse selection\nscreen:\n{mouse_rows}")
    before_mouse_config = display_config.read_text(encoding="utf-8")
    # Move selection away from the current dark row and back; neither click may persist.
    for label, row in (("light", light_row), ("dark", dark_row)):
        row_number, row_text = row
        column = max(1, len(row_text) - len(row_text.lstrip()) + 4)
        before_mouse = capture(tmux_exe, session)
        send_literal(tmux_exe, session, f"\x1b[<0;{column};{row_number}M")
        after_mouse = wait_for_screen_change(
            tmux_exe, session, before_mouse, f"display mouse selection redraw ({label})"
        )
        if display_config.read_text(encoding="utf-8") != before_mouse_config:
            raise RuntimeError(f"settings mouse selection wrote display.json on {label}")
        if "Stored TUI theme" in after_mouse:
            raise RuntimeError(f"settings mouse selection confirmed a theme write\nscreen:\n{after_mouse}")
    save_evidence(root, "nested-settings-mouse-no-persist", capture(tmux_exe, session))

    close_settings(tmux_exe, session, "nested settings closed")
    send_keys(tmux_exe, session, "C-u")

    # Workspace trust persists once through the backend /trust command, refreshes the
    # current row, and adds zero chat/transcript receipts (no live provider involved).
    # Synchronize on backend authority (the persisted trust record and the refreshed
    # current row), not on transient status text that a concurrent reload can replace.
    trust_file = ctx.state / "ava" / "project-trust.json"
    open_settings_section(
        tmux_exe,
        session,
        "Workspace",
        r"Settings › Workspace|Project trust|Trust project",
        "workspace section before trust action",
    )
    send_literal(tmux_exe, session, "Trust project")
    # Tolerate a first character lost to a concurrent settings-view rebuild (the same
    # race open_settings_section already accommodates for root queries).
    wait_for(tmux_exe, session, r"Search\s{2}(?:Trust project|rust project)", "trust project row filter")
    # The best fuzzy match auto-selects; confirm the action row (not the Project trust
    # status row) is selected before Enter instead of forcing the first visible row.
    wait_for(tmux_exe, session, r"›\s*Trust project", "trust project row selected")
    send_keys(tmux_exe, session, "Enter")
    # Non-display confirms return to the composer; synchronize on the closed modal.
    after_trust_action = wait_for_absent(tmux_exe, session, r"Settings ›", "trust action closed settings")
    _assert_no_settings_chat_receipt(after_trust_action, "workspace trust", r"trusted project resources")
    save_evidence(root, "nested-settings-trust-project", after_trust_action)
    deadline = time.monotonic() + 8.0
    trust_disk = trust_file.read_text(encoding="utf-8") if trust_file.exists() else ""
    while time.monotonic() < deadline and '"trusted":true' not in trust_disk:
        time.sleep(0.05)
        trust_disk = trust_file.read_text(encoding="utf-8") if trust_file.exists() else ""
    if '"trusted":true' not in trust_disk:
        raise RuntimeError(f"trust action did not persist the trusted decision\npath:\n{trust_file}")
    open_settings_section(
        tmux_exe,
        session,
        "Workspace",
        r"Settings › Workspace|Project trust",
        "workspace section after trust action",
    )
    trust_row = wait_for(tmux_exe, session, r"Project trust[^\n]*trusted", "trust row refreshed to trusted")
    save_evidence(root, "nested-settings-trust-row-refreshed", trust_row)
    close_settings(tmux_exe, session, "closed after trust row refresh")
    send_keys(tmux_exe, session, "C-u")

    # Restore the unknown decision so the isolated config stays neutral for later checks.
    open_settings_section(
        tmux_exe,
        session,
        "Workspace",
        r"Settings › Workspace|Clear trust decision",
        "workspace section before trust clear",
    )
    send_literal(tmux_exe, session, "Clear trust decision")
    wait_for(tmux_exe, session, r"Search\s{2}(?:Clear trust decision|lear trust decision)", "clear trust row filter")
    wait_for(tmux_exe, session, r"›\s*Clear trust decision", "clear trust row selected")
    send_keys(tmux_exe, session, "Enter")
    after_trust_clear = wait_for_absent(tmux_exe, session, r"Settings ›", "trust clear closed settings")
    _assert_no_settings_chat_receipt(after_trust_clear, "workspace trust clear", r"cleared project trust decision")
    deadline = time.monotonic() + 8.0
    trust_disk = trust_file.read_text(encoding="utf-8") if trust_file.exists() else ""
    while time.monotonic() < deadline and '"trusted":true' in trust_disk:
        time.sleep(0.05)
        trust_disk = trust_file.read_text(encoding="utf-8") if trust_file.exists() else ""
    if '"trusted":true' in trust_disk:
        raise RuntimeError(f"trust clear did not remove the persisted decision\npath text:\n{trust_disk}")

    # Input-section keybinding reload applies live bindings with zero chat receipts.
    # Stage a distinctive binding, reload through settings, then prove the live binding
    # works instead of synchronizing on transient status text.
    (ava_config / "keybinds.json").write_text(
        '{"tui.editor.cursorLineStart":["Home","Ctrl+A"],"tui.editor.cursorLineEnd":["F2"]}\n',
        encoding="utf-8",
    )
    open_settings_section(
        tmux_exe,
        session,
        "Input",
        r"Settings › Input|Reload",
        "input section before keybinding reload",
    )
    send_literal(tmux_exe, session, "reload")
    wait_for(tmux_exe, session, r"Search\s{2}(?:reload|eload)", "keybinding reload row filter")
    wait_for(tmux_exe, session, r"›\s*Reload", "keybinding reload row selected")
    send_keys(tmux_exe, session, "Enter")
    reloaded = wait_for_absent(tmux_exe, session, r"Settings ›", "keybinding reload closed settings")
    _assert_no_settings_chat_receipt(reloaded, "keybinding reload", r"keybindings reloaded")
    save_evidence(root, "nested-settings-keybinding-reload", reloaded)
    send_literal(tmux_exe, session, "abc")
    send_keys(tmux_exe, session, "C-a")
    send_keys(tmux_exe, session, "F2")
    send_literal(tmux_exe, session, "Z")
    live_binding = wait_for(tmux_exe, session, r"abcZ", "reloaded F2 cursor-end binding")
    save_evidence(root, "nested-settings-keybinding-reload-live", live_binding)
    send_keys(tmux_exe, session, "C-u")
    (ava_config / "keybinds.json").write_text(
        '{"tui.editor.cursorLineStart":["Home","Ctrl+A"]}\n', encoding="utf-8"
    )

    tmux(tmux_exe, "kill-session", "-t", session, check=False)
