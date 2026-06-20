#!/usr/bin/env python3
import argparse
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import time
import uuid


SKIP = 77


def enabled(value) -> bool:
    return value is not None and value.lower() in {"1", "true", "yes", "on"}


def run(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess:
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def tmux(tmux_exe: str, *args: str, check: bool = True) -> subprocess.CompletedProcess:
    return run([tmux_exe, *args], check=check)


def capture(tmux_exe: str, session: str) -> str:
    result = tmux(tmux_exe, "capture-pane", "-t", f"{session}:0.0", "-p")
    lines = [line.rstrip() for line in result.stdout.splitlines()]
    return "\n".join(lines)


def capture_styled(tmux_exe: str, session: str) -> str:
    result = tmux(tmux_exe, "capture-pane", "-e", "-t", f"{session}:0.0", "-p")
    return result.stdout


def wait_for(tmux_exe: str, session: str, pattern: str, label: str, timeout: float = 8.0) -> str:
    compiled = re.compile(pattern)
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        status = tmux(tmux_exe, "has-session", "-t", session, check=False)
        if status.returncode != 0:
            raise RuntimeError(f"tmux session exited before {label}\nlast screen:\n{last}")
        last = capture(tmux_exe, session)
        if compiled.search(last):
            return last
        time.sleep(0.1)
    raise RuntimeError(f"timed out waiting for {label}; expected /{pattern}/\nlast screen:\n{last}")


def wait_for_absent(tmux_exe: str, session: str, pattern: str, label: str, timeout: float = 8.0) -> str:
    compiled = re.compile(pattern)
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        status = tmux(tmux_exe, "has-session", "-t", session, check=False)
        if status.returncode != 0:
            raise RuntimeError(f"tmux session exited before {label}\nlast screen:\n{last}")
        last = capture(tmux_exe, session)
        if not compiled.search(last):
            return last
        time.sleep(0.1)
    raise RuntimeError(f"timed out waiting for {label}; still matched /{pattern}/\nlast screen:\n{last}")


def send_keys(tmux_exe: str, session: str, *keys: str) -> None:
    tmux(tmux_exe, "send-keys", "-t", f"{session}:0.0", *keys)


def send_literal(tmux_exe: str, session: str, text: str) -> None:
    tmux(tmux_exe, "send-keys", "-t", f"{session}:0.0", "-l", text)


def wait_for_session_exit(tmux_exe: str, session: str, timeout: float = 8.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = tmux(tmux_exe, "has-session", "-t", session, check=False)
        if status.returncode != 0:
            return
        time.sleep(0.1)
    screen = capture(tmux_exe, session)
    raise RuntimeError(f"tmux session did not exit\nscreen:\n{screen}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ava", required=True)
    parser.add_argument("--root", required=True)
    args = parser.parse_args()

    if not enabled(os.environ.get("AVA_TUI_TMUX_SMOKE")):
        print("skipping tmux TUI smoke; set AVA_TUI_TMUX_SMOKE=1 to run")
        return SKIP

    tmux_exe = shutil.which("tmux")
    if tmux_exe is None:
        print("skipping tmux TUI smoke; tmux is not installed")
        return SKIP

    ava_exe = pathlib.Path(args.ava).resolve()
    if not ava_exe.exists():
        raise RuntimeError(f"AVA executable does not exist: {ava_exe}")

    root = pathlib.Path(args.root).resolve()
    if root.exists():
        if root.name != "tui-tmux-smoke":
            raise RuntimeError(f"refusing to clear unexpected smoke root: {root}")
        shutil.rmtree(root)
    workspace = root / "workspace"
    home = root / "home"
    config = root / "config"
    state = root / "state"
    data = root / "data"
    for path in (workspace, home, config, state, data):
        path.mkdir(parents=True, exist_ok=True)
    ava_config = config / "ava"
    ava_config.mkdir(parents=True, exist_ok=True)
    (workspace / "src").mkdir(parents=True, exist_ok=True)
    (workspace / "src" / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
    (workspace / "my folder").mkdir(parents=True, exist_ok=True)
    (workspace / "my folder" / "space file.txt").write_text("space path\n", encoding="utf-8")

    conflict_session = f"ava-tui-conflict-{uuid.uuid4().hex[:10]}"
    session = f"ava-tui-smoke-{uuid.uuid4().hex[:10]}"
    tmux(tmux_exe, "kill-session", "-t", conflict_session, check=False)
    tmux(tmux_exe, "kill-session", "-t", session, check=False)

    env_prefix = (
        f"HOME={shlex.quote(str(home))} "
        f"XDG_CONFIG_HOME={shlex.quote(str(config))} "
        f"XDG_STATE_HOME={shlex.quote(str(state))} "
        f"XDG_DATA_HOME={shlex.quote(str(data))} "
        "NO_COLOR=1 "
        f"exec {shlex.quote(str(ava_exe))}"
    )

    try:
        (ava_config / "keybinds.json").write_text(
            '{"submit":"Ctrl+P","model_cycle_forward":"Ctrl+P"}\n', encoding="utf-8"
        )
        tmux(
            tmux_exe,
            "new-session",
            "-d",
            "-s",
            conflict_session,
            "-x",
            "120",
            "-y",
            "32",
            "-c",
            str(workspace),
            env_prefix,
        )
        conflict_screen = wait_for(
            tmux_exe,
            conflict_session,
            r"conflicting TUI keybinding|key: Ctrl\+P",
            "keybinding conflict startup diagnostic",
        )
        if "conflicting TUI keybinding" not in conflict_screen or "Ctrl+P" not in conflict_screen:
            raise RuntimeError(f"keybinding conflict diagnostic did not render visibly\nscreen:\n{conflict_screen}")
        tmux(tmux_exe, "kill-session", "-t", conflict_session, check=False)

        (ava_config / "keybinds.json").write_text(
            '{"tui.editor.cursorLineStart":["Home","Ctrl+A"]}\n', encoding="utf-8"
        )
        tmux(
            tmux_exe,
            "new-session",
            "-d",
            "-s",
            session,
            "-x",
            "120",
            "-y",
            "32",
            "-c",
            str(workspace),
            env_prefix,
        )

        initial = wait_for(tmux_exe, session, r"Type a message|live session", "initial TUI frame")
        if "AVA" not in initial:
            raise RuntimeError(f"initial frame did not show AVA branding\nscreen:\n{initial}")
        styled_initial = capture_styled(tmux_exe, session)
        if "\x1b[" in styled_initial:
            raise RuntimeError(f"NO_COLOR=1 TUI frame still captured ANSI style escapes\nscreen:\n{styled_initial}")

        send_literal(tmux_exe, session, "/settings")
        wait_for(tmux_exe, session, r"/settings", "settings command draft")
        send_keys(tmux_exe, session, "Enter")
        settings_modal = wait_for(tmux_exe, session, r"Settings|Search settings", "settings modal")
        if "plain" not in settings_modal or "NO_COLOR" not in settings_modal:
            raise RuntimeError(f"settings modal did not report the active NO_COLOR plain mode\nscreen:\n{settings_modal}")
        styled_settings = capture_styled(tmux_exe, session)
        if "\x1b[" in styled_settings:
            raise RuntimeError(f"NO_COLOR=1 settings modal still captured ANSI style escapes\nscreen:\n{styled_settings}")
        send_keys(tmux_exe, session, "Escape")
        wait_for_absent(tmux_exe, session, r"Search settings", "settings modal canceled")

        send_literal(tmux_exe, session, "/copy")
        wait_for(tmux_exe, session, r"/copy", "empty copy command draft")
        send_keys(tmux_exe, session, "Enter")
        empty_copy = wait_for(tmux_exe, session, r"no AVA messages to copy", "empty /copy status")
        if "no AVA messages to copy" not in empty_copy:
            raise RuntimeError(f"/copy without prior AVA messages did not report the empty copy state\nscreen:\n{empty_copy}")

        send_literal(tmux_exe, session, "\x1b[Z")
        shift_tab_reasoning = wait_for(tmux_exe, session, r"reasoning set to low|reasoning low", "shift-tab reasoning cycle")
        if "reasoning set to low" not in shift_tab_reasoning and "reasoning low" not in shift_tab_reasoning:
            raise RuntimeError(f"Shift+Tab did not cycle the visible reasoning state\nscreen:\n{shift_tab_reasoning}")
        send_keys(tmux_exe, session, "C-p")
        model_cycle = wait_for(tmux_exe, session, r"model cycled|GPT-4\.1 mini|gpt-4\.1-mini", "ctrl-p model cycle")
        if "model cycled" not in model_cycle and "GPT-4.1 mini" not in model_cycle and "gpt-4.1-mini" not in model_cycle:
            raise RuntimeError(f"Ctrl+P did not cycle the visible model state\nscreen:\n{model_cycle}")
        send_keys(tmux_exe, session, "C-l")
        model_selector = wait_for(tmux_exe, session, r"Select model|Search models", "ctrl-l model selector")
        if "Select model" not in model_selector and "Search models" not in model_selector:
            raise RuntimeError(f"Ctrl+L did not open the model selector\nscreen:\n{model_selector}")
        send_keys(tmux_exe, session, "Escape")
        wait_for_absent(tmux_exe, session, r"Select model|Search models", "model selector canceled")
        (ava_config / "keybinds.json").write_text(
            '{"tui.editor.cursorLineStart":["Home","Ctrl+A","Alt+Up"],'
            '"tui.editor.cursorLeft":["Left","Ctrl+B","Alt+H"],'
            '"tui.editor.cursorWordRight":["Ctrl+Right","Alt+Right","Alt+F","Alt+W"],'
            '"tui.editor.deleteCharBackward":["Ctrl+H"],'
            '"tui.select.confirm":["Enter","Space"],'
            '"tui.select.cancel":["Escape","Ctrl+W"]}\n',
            encoding="utf-8",
        )
        send_literal(tmux_exe, session, "/reload")
        wait_for(tmux_exe, session, r"/reload.*Reload keybindings", "reload palette row")
        send_keys(tmux_exe, session, "Escape")
        wait_for_absent(tmux_exe, session, r"/reload.*Reload keybindings", "reload palette dismissed")
        send_keys(tmux_exe, session, "Enter")
        reload_screen = wait_for(tmux_exe, session, r"keybindings reloaded", "live keybinding reload")
        if "keybindings reloaded" not in reload_screen:
            raise RuntimeError(f"/reload did not report a live keybinding reload\nscreen:\n{reload_screen}")
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
        send_literal(tmux_exe, session, "ctrl-enter-one")
        send_literal(tmux_exe, session, "\x1b[13;5u")
        send_literal(tmux_exe, session, "alt-enter-two")
        send_literal(tmux_exe, session, "\x1b\r")
        send_literal(tmux_exe, session, "tail")
        modified_enter = wait_for(
            tmux_exe,
            session,
            r"ctrl-enter-one[^\n]*\n[^\n]*alt-enter-two[^\n]*\n[^\n]*tail",
            "modified Enter newline aliases",
        )
        if "ctrl-enter-onealt-enter-two" in modified_enter or "alt-enter-twotail" in modified_enter:
            raise RuntimeError(f"modified Enter shortcuts did not create multiline draft breaks\nscreen:\n{modified_enter}")
        send_keys(tmux_exe, session, "C-c")
        wait_for_absent(tmux_exe, session, r"ctrl-enter-one|alt-enter-two|tail", "modified Enter draft clear")

        send_literal(tmux_exe, session, "/")
        palette = wait_for(tmux_exe, session, r"/help|Show commands", "slash palette")
        if "[200~" in palette or "[201~" in palette:
            raise RuntimeError(f"paste markers leaked before paste smoke\nscreen:\n{palette}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/per")
        permissions_palette = wait_for(
            tmux_exe, session, r"/permissions|permission rules", "permission rule command palette"
        )
        if "/permissions" not in permissions_palette and "permission rules" not in permissions_palette:
            raise RuntimeError(f"permission command did not appear in the slash palette\nscreen:\n{permissions_palette}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/permissions list")
        send_keys(tmux_exe, session, "Enter")
        permissions_list = wait_for(tmux_exe, session, r"Permission rules:|No persistent permission rules", "permission rules command output")
        if "Permission rules:" not in permissions_list and "No persistent permission rules" not in permissions_list:
            raise RuntimeError(f"permission rules command did not render command output\nscreen:\n{permissions_list}")

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
        wait_for(tmux_exe, session, r"/reload.*Reload keybindings", "restore default select bindings reload row")
        send_keys(tmux_exe, session, "Escape")
        wait_for_absent(tmux_exe, session, r"/reload.*Reload keybindings", "restore default select bindings reload dismissed")
        send_keys(tmux_exe, session, "Enter")
        wait_for(tmux_exe, session, r"keybindings reloaded", "default select bindings restored")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/read sr")
        path_palette = wait_for(tmux_exe, session, r"src/main\.cpp|src/", "slash path completion palette")
        if "src/main.cpp" not in path_palette and "src/" not in path_palette:
            raise RuntimeError(f"slash path completion did not show workspace paths\nscreen:\n{path_palette}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "review @sr")
        reference_palette = wait_for(tmux_exe, session, r"@src/main\.cpp|@src/", "file reference completion palette")
        if "@src/main.cpp" not in reference_palette and "@src/" not in reference_palette:
            raise RuntimeError(f"file reference completion did not show workspace paths\nscreen:\n{reference_palette}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "review @my")
        spaced_reference_palette = wait_for(
            tmux_exe, session, r'@"my folder/"', "quoted file reference completion palette"
        )
        if '@"my folder/"' not in spaced_reference_palette:
            raise RuntimeError(
                f"quoted file reference completion did not show a path with spaces\nscreen:\n{spaced_reference_palette}"
            )

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "inspect src/")
        prompt_path_palette = wait_for(tmux_exe, session, r"src/main\.cpp", "normal prompt path completion palette")
        if "src/main.cpp" not in prompt_path_palette:
            raise RuntimeError(f"normal prompt path completion did not show workspace paths\nscreen:\n{prompt_path_palette}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "main")
        send_keys(tmux_exe, session, "Tab")
        forced_path_completion = wait_for(tmux_exe, session, r"src/main\.cpp", "forced bare-token path completion")
        if "src/main.cpp" not in forced_path_completion:
            raise RuntimeError(f"forced path completion did not insert workspace path\nscreen:\n{forced_path_completion}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/bash git push origin main")
        send_keys(tmux_exe, session, "Enter")
        permission = wait_for(tmux_exe, session, r"PERMISSION REQUIRED.*risk high|risk high.*reason command can change", "permission prompt risk metadata")
        if "risk high" not in permission or "reason command can change external or destructive state" not in permission:
            raise RuntimeError(f"permission prompt did not expose risk and reason metadata\nscreen:\n{permission}")
        if "Deny rule" not in permission or "Allow rule" not in permission:
            raise RuntimeError(f"permission prompt did not expose remembered rule choices\nscreen:\n{permission}")
        send_keys(tmux_exe, session, "Tab", "Tab", "Enter")
        wait_for_absent(tmux_exe, session, r"PERMISSION REQUIRED", "permission prompt denied")
        denied_card = wait_for(
            tmux_exe,
            session,
            r"permission deny.*reason command can change|reason command can change.*permission deny",
            "permission denial tool-card audit",
        )
        if "permission deny" not in denied_card or "reason command can change" not in denied_card:
            raise RuntimeError(f"permission denial did not remain visible on the tool card\nscreen:\n{denied_card}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/permissions list")
        send_keys(tmux_exe, session, "Enter")
        remembered_rule = wait_for(
            tmux_exe,
            session,
            r"(?s)permrule_.*git push origin main",
            "remembered permission rule listing",
        )
        if "git push origin main" not in remembered_rule or "deny bash" not in remembered_rule:
            raise RuntimeError(f"remembered deny rule was not listed\nscreen:\n{remembered_rule}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/bash git push origin main")
        send_keys(tmux_exe, session, "Enter")
        time.sleep(0.4)
        repeated_denial = capture(tmux_exe, session)
        if "PERMISSION REQUIRED" in repeated_denial:
            raise RuntimeError(f"remembered deny rule did not suppress a repeated prompt\nscreen:\n{repeated_denial}")
        send_keys(tmux_exe, session, "C-o")
        expanded_tool_details = wait_for(
            tmux_exe,
            session,
            r"(?s)permission: deny.*command: git push origin main|command: git push origin main.*permission: deny",
            "ctrl-o tool detail expansion",
        )
        if "permission: deny" not in expanded_tool_details or "command: git push origin main" not in expanded_tool_details:
            raise RuntimeError(f"Ctrl+O did not expand the visible permission tool-card details\nscreen:\n{expanded_tool_details}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/copy tool")
        send_keys(tmux_exe, session, "Enter")
        copied_tool = wait_for(tmux_exe, session, r"copied latest tool details to clipboard", "copy latest tool details")
        if "copied latest tool details to clipboard" not in copied_tool:
            raise RuntimeError(f"/copy tool did not report a copied tool-detail payload\nscreen:\n{copied_tool}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/write src/main.cpp int changed() { return 1; }")
        send_keys(tmux_exe, session, "Enter")
        write_result = wait_for(
            tmux_exe,
            session,
            r"(?s)wrote .*src/main\.cpp|PERMISSION REQUIRED",
            "write command result for diff copy",
        )
        if "PERMISSION REQUIRED" in write_result:
            send_keys(tmux_exe, session, "Tab", "Enter")
            write_result = wait_for(tmux_exe, session, r"(?s)wrote .*src/main\.cpp", "allowed write command result")
        if "wrote" not in write_result or "src/main.cpp" not in write_result:
            raise RuntimeError(f"/write did not render a successful mutation tool card\nscreen:\n{write_result}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/copy diff")
        send_keys(tmux_exe, session, "Enter")
        copied_diff = wait_for(tmux_exe, session, r"copied latest tool diff to clipboard", "copy latest tool diff")
        if "copied latest tool diff to clipboard" not in copied_diff:
            raise RuntimeError(f"/copy diff did not report a copied unified diff\nscreen:\n{copied_diff}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/permissions audit git push")
        send_keys(tmux_exe, session, "Enter")
        permission_audit = wait_for(
            tmux_exe,
            session,
            r"(?s)session permission decisions.*git push origin",
            "permission audit command output",
        )
        if "session permission decisions" not in permission_audit or "git push origin" not in permission_audit:
            raise RuntimeError(f"permission audit command did not render the denied command\nscreen:\n{permission_audit}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/name TUI smoke")
        send_keys(tmux_exe, session, "Enter")
        wait_for(tmux_exe, session, r"session name set: \"TUI smoke\"", "session name command")
        send_keys(tmux_exe, session, "Up")
        wait_for(tmux_exe, session, r"❯ /name TUI smoke", "composer input history recall")
        send_keys(tmux_exe, session, "C-c")
        wait_for_absent(tmux_exe, session, r"❯ /name TUI smoke", "composer recalled history clear")

        for index in range(1, 7):
            send_keys(tmux_exe, session, "C-u")
            send_literal(tmux_exe, session, f"/new Page {index}")
            send_keys(tmux_exe, session, "Enter")
            wait_for(tmux_exe, session, rf"started session session_.*Page {index}", f"seed page session {index}")

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/resume")
        wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette row")
        send_keys(tmux_exe, session, "Escape")
        wait_for_absent(tmux_exe, session, r"/resume.*Resume a session", "resume palette dismissed")
        send_keys(tmux_exe, session, "Enter")
        selector = wait_for(tmux_exe, session, r"Select session|Session tree", "resume session selector")
        if "session selector opened" not in selector and "Select session" not in selector:
            raise RuntimeError(f"/resume did not open the session selector\nscreen:\n{selector}")
        send_literal(tmux_exe, session, "\x1b[6~")
        page_down = wait_for(tmux_exe, session, r"›\s+Page 1", "session selector page down")
        if "Page 1" not in page_down:
            raise RuntimeError(f"session selector PageDown did not jump by a page\nscreen:\n{page_down}")
        send_literal(tmux_exe, session, "\x1b[5~")
        page_up = wait_for(tmux_exe, session, r"›\s+(?:●\s+)?Page 6", "session selector page up")
        if "Page 6" not in page_up:
            raise RuntimeError(f"session selector PageUp did not jump by a page\nscreen:\n{page_up}")
        send_literal(tmux_exe, session, "TUI smoke")
        wait_for(tmux_exe, session, r"›\s+TUI smoke", "session selector query after page navigation")
        send_keys(tmux_exe, session, "C-s")
        wait_for(tmux_exe, session, r"sort name|Ctrl\+S/Ctrl\+T sort \(name\)", "session selector sort cycle")
        send_keys(tmux_exe, session, "C-n")
        named_filter = wait_for(tmux_exe, session, r"named only|med only|Ctrl\+N sho", "session selector named-only filter")
        if "TUI smoke" not in named_filter or (
            "named only" not in named_filter and "med only" not in named_filter and "Ctrl+N sho" not in named_filter
        ):
            raise RuntimeError(f"session selector named-only filter did not keep the named session visible\nscreen:\n{named_filter}")
        send_keys(tmux_exe, session, "C-p")
        path_toggle = wait_for(tmux_exe, session, r"paths hidden", "session selector path-display toggle")
        if "TUI smoke" not in path_toggle or "paths hidden" not in path_toggle:
            raise RuntimeError(f"session selector path-display toggle did not keep the named session visible\nscreen:\n{path_toggle}")
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
        send_keys(tmux_exe, session, "C-l")
        labels_draft = wait_for(tmux_exe, session, r"/sessions labels session_", "session selector labels draft")
        if "/sessions labels session_" not in labels_draft:
            raise RuntimeError(f"session selector Ctrl+L did not restore a labels command draft\nscreen:\n{labels_draft}")
        send_literal(tmux_exe, session, "picker bookmark")
        send_keys(tmux_exe, session, "Enter")
        wait_for(tmux_exe, session, r"session .* labels set: picker,bookmark", "session selector labels command")
        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/sessions picker")
        send_keys(tmux_exe, session, "Enter")
        wait_for(
            tmux_exe,
            session,
            r"(?s)Sessions:.*Selector rename.*labels=picker,bookmark",
            "session selector labels visible in tree",
        )

        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/new Archive current")
        send_keys(tmux_exe, session, "Enter")
        wait_for(tmux_exe, session, r"started session session_", "new session before selector archive")
        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/resume")
        wait_for(tmux_exe, session, r"/resume.*Resume a session", "resume palette before archive")
        send_keys(tmux_exe, session, "Escape")
        wait_for_absent(tmux_exe, session, r"/resume.*Resume a session", "resume palette dismissed before archive")
        send_keys(tmux_exe, session, "Enter")
        wait_for(tmux_exe, session, r"Select session|Session tree", "resume selector before archive")
        send_literal(tmux_exe, session, "Selector rename")
        wait_for(tmux_exe, session, r"›\s+Selector rename", "session selector non-current row selected")
        send_keys(tmux_exe, session, "C-d")
        time.sleep(0.2)
        send_keys(tmux_exe, session, "C-d")
        time.sleep(0.2)
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
        archived_selector = wait_for(tmux_exe, session, r"archived shown", "session selector archived toggle")
        send_literal(tmux_exe, session, "Selector rename")
        archived_selector = wait_for(
            tmux_exe, session, r"(?s)›.*Selector rename.*archived|archived.*›.*Selector rename", "archived row filtered in selector"
        )
        if "archived" not in archived_selector:
            raise RuntimeError(f"session selector did not show archived session state\nscreen:\n{archived_selector}")
        send_keys(tmux_exe, session, "C-d")
        time.sleep(0.2)
        send_keys(tmux_exe, session, "C-d")
        time.sleep(0.2)
        send_keys(tmux_exe, session, "C-c")
        wait_for_absent(tmux_exe, session, r"Select session|Session tree", "session selector closed after restore")
        send_keys(tmux_exe, session, "C-u")
        send_literal(tmux_exe, session, "/sessions Selector rename")
        send_keys(tmux_exe, session, "Enter")
        wait_for(tmux_exe, session, r"(?s)Sessions:.*Selector rename", "restored session visible in default list")

        large_paste = "\n".join(f"line{i:02d}" for i in range(1, 12))
        send_literal(tmux_exe, session, f"\x1b[200~{large_paste}\x1b[201~")
        paste_marker = wait_for(tmux_exe, session, r"\[paste #1 \+11 lines\]", "large bracketed paste marker")
        if "line11" in paste_marker:
            raise RuntimeError(f"large paste content leaked instead of collapsing to a marker\nscreen:\n{paste_marker}")
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
        wait_for(tmux_exe, session, r"❯ AB|^AB$", "large paste marker atomic forward delete")
        send_keys(tmux_exe, session, "C-c")
        wait_for_absent(tmux_exe, session, r"A\[paste #1 \+11 lines\]B|❯ AB", "large paste marker forward delete clear")

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

        send_literal(tmux_exe, session, "one two three")
        send_keys(tmux_exe, session, "C-a", "M-f", "M-d")
        forward_word_delete = wait_for(tmux_exe, session, r"❯ one three|one three", "alt-d forward word deletion")
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
        wait_for(tmux_exe, session, r"❯ undo|undo", "ctrl-w draft before ctrl-minus undo")
        send_literal(tmux_exe, session, "\x1f")
        wait_for(tmux_exe, session, r"undo word", "ctrl-minus undo restores draft")
        send_keys(tmux_exe, session, "C-c")
        wait_for_absent(tmux_exe, session, r"undo word", "ctrl-minus undo draft clear")

        send_literal(tmux_exe, session, "\x1b[200~first\nsecond\x1b[201~")
        wait_for(tmux_exe, session, r"first.*second|first", "multiline draft before vertical cursor")
        send_keys(tmux_exe, session, "Up")
        send_literal(tmux_exe, session, "X")
        moved = wait_for(tmux_exe, session, r"firstX", "multiline draft arrow-up cursor movement")
        if "secondX" in moved:
            raise RuntimeError(f"arrow-up edited the second line instead of the previous line\nscreen:\n{moved}")
        send_keys(tmux_exe, session, "C-c")
        wait_for_absent(tmux_exe, session, r"firstX|second", "multiline draft clear")

        send_literal(tmux_exe, session, "\x1b[200~abcdef\nx\nabcdef\x1b[201~")
        wait_for(tmux_exe, session, r"abcdef", "multiline sticky-column draft")
        send_keys(tmux_exe, session, "Up", "Up")
        send_literal(tmux_exe, session, "Z")
        sticky = wait_for(tmux_exe, session, r"abcdefZ", "multiline sticky-column cursor movement")
        if "xZ" in sticky:
            raise RuntimeError(f"sticky-column movement collapsed to the short middle line\nscreen:\n{sticky}")
        send_keys(tmux_exe, session, "C-c")
        wait_for_absent(tmux_exe, session, r"abcdefZ", "sticky-column draft clear")

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
        send_keys(tmux_exe, session, "Up", "C-k")
        wait_for(tmux_exe, session, r"joinline", "ctrl-k line-end join")
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

        send_keys(tmux_exe, session, "C-c")
        time.sleep(0.2)
        if tmux(tmux_exe, "has-session", "-t", session, check=False).returncode != 0:
            return 0
        wait_for_absent(tmux_exe, session, r"alpha|beta", "draft clear before quit")
        send_keys(tmux_exe, session, "C-d")
        wait_for_session_exit(tmux_exe, session)
        return 0
    finally:
        tmux(tmux_exe, "kill-session", "-t", conflict_session, check=False)
        tmux(tmux_exe, "kill-session", "-t", session, check=False)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
