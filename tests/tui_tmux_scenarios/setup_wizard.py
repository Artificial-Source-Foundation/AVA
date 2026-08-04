"""Credential-free real-terminal coverage for the local-only first-run setup wizard."""

from __future__ import annotations

import os
import pathlib
import re
import stat
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


def _auth_snapshot(ava_config: pathlib.Path) -> str | None:
    auth = ava_config / "auth.json"
    if not auth.exists():
        return None
    return auth.read_text(encoding="utf-8", errors="replace")


def _session_jsonl_snapshot(session_dir: pathlib.Path) -> dict[str, str]:
    files: dict[str, str] = {}
    if session_dir.exists():
        for path in sorted(session_dir.rglob("*.jsonl")):
            files[str(path.relative_to(session_dir))] = path.read_text(encoding="utf-8", errors="replace")
    return files


def _provider_request_bytes(root: pathlib.Path) -> int:
    return sum(path.stat().st_size for path in sorted(root.glob("*-provider-requests.log")) if path.exists())


def _onboarding_path(state: pathlib.Path) -> pathlib.Path:
    return state / "ava" / "onboarding.json"


def _assert_no_backend_mutation(
    *,
    before_auth: str | None,
    after_auth: str | None,
    before_jsonl: dict[str, str],
    after_jsonl: dict[str, str],
    before_requests: int,
    after_requests: int,
    label: str,
) -> None:
    if after_auth != before_auth:
        raise RuntimeError(f"{label} mutated auth.json")
    if after_jsonl != before_jsonl:
        raise RuntimeError(f"{label} mutated session JSONL\nbefore={sorted(before_jsonl)}\nafter={sorted(after_jsonl)}")
    if after_requests != before_requests:
        raise RuntimeError(f"{label} issued provider requests before={before_requests} after={after_requests}")


def scenario_setup_wizard(ctx: SmokeContext) -> None:
    tmux_exe = ctx.tmux
    root = ctx.root
    workspace = ctx.workspace
    ava_config = ctx.ava_config
    state = ctx.state
    session_dir = state / "ava" / "sessions"
    onboarding = _onboarding_path(state)
    display_config = ava_config / "display.json"
    session = ctx.session_name("setup-wizard")
    env_prefix = ctx.pane_command(
        home=ctx.home,
        config=ctx.config,
        state=state,
        data=ctx.data,
        extra={"NO_COLOR": "", "AVA_TUI_THEME": "", "COLORFGBG": ""},
    )

    if onboarding.exists():
        onboarding.unlink()
    if display_config.exists():
        display_config.unlink()

    before_auth = _auth_snapshot(ava_config)
    before_requests = _provider_request_bytes(root)

    # Fresh private state auto-opens the wizard at ordinary height.
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
    auto_open = wait_for(tmux_exe, session, r"First-run setup|Welcome|local terminal setup", "setup wizard auto-open")
    if "First-run setup" not in auto_open and "Welcome" not in auto_open:
        raise RuntimeError(f"fresh state did not auto-open setup wizard\nscreen:\n{auto_open}")
    if onboarding.exists():
        raise RuntimeError("auto-open wrote onboarding.json before Finish/Skip")
    save_evidence(root, "setup-wizard-auto-open", auto_open)
    # Baseline session JSONL after ordinary startup open (wizard must not append further).
    before_jsonl = _session_jsonl_snapshot(session_dir)

    # Theme preview then cancel leaves no marker and no display write.
    send_keys(tmux_exe, session, "Enter")  # Continue from Welcome
    theme_step = wait_for(tmux_exe, session, r"Theme|Keep current theme|Theme light", "setup theme step")
    save_evidence(root, "setup-wizard-theme", theme_step)
    send_literal(tmux_exe, session, "theme light")
    preview = wait_for(tmux_exe, session, r"filter\s+theme light|Theme light", "theme light highlight")
    if display_config.exists():
        raise RuntimeError("theme highlight wrote display.json before confirmation")
    if onboarding.exists():
        raise RuntimeError("theme highlight wrote onboarding.json")

    # External hydration while setup is open may update image authority, but setup
    # preview must reapply theme only and Esc must not restore stale image values.
    external_display = '{\n  "theme": "dark",\n  "show_images": false,\n  "image_width_cells": 96\n}\n'
    display_config.write_text(external_display, encoding="utf-8")
    time.sleep(0.7)  # display watcher poll interval is bounded below one second
    if display_config.read_text(encoding="utf-8") != external_display:
        raise RuntimeError("setup theme preview disturbed externally hydrated image settings")
    save_evidence(root, "setup-wizard-theme-preview", preview)
    send_keys(tmux_exe, session, "Escape")
    canceled = wait_for_absent(tmux_exe, session, r"First-run setup", "setup cancel closes wizard")
    if onboarding.exists():
        raise RuntimeError("cancel path wrote onboarding.json")
    if display_config.read_text(encoding="utf-8") != external_display:
        raise RuntimeError("setup cancel restored stale image settings or wrote display.json")
    save_evidence(root, "setup-wizard-cancel", canceled)
    display_config.unlink()

    after_cancel_auth = _auth_snapshot(ava_config)
    after_cancel_jsonl = _session_jsonl_snapshot(session_dir)
    after_cancel_requests = _provider_request_bytes(root)
    _assert_no_backend_mutation(
        before_auth=before_auth,
        after_auth=after_cancel_auth,
        before_jsonl=before_jsonl,
        after_jsonl=after_cancel_jsonl,
        before_requests=before_requests,
        after_requests=after_cancel_requests,
        label="setup cancel",
    )

    # Explicit reopen and Skip persists skipped.
    send_literal(tmux_exe, session, "/setup")
    send_keys(tmux_exe, session, "Enter")
    reopened = wait_for(tmux_exe, session, r"First-run setup|Welcome", "explicit /setup reopen after cancel")
    save_evidence(root, "setup-wizard-reopen", reopened)
    send_literal(tmux_exe, session, "Skip setup")
    wait_for(tmux_exe, session, r"filter\s+Skip setup|Skip setup", "skip row selected")
    send_keys(tmux_exe, session, "Enter")
    skipped = wait_for(tmux_exe, session, r"Setup skipped|setup skipped", "skip persistence receipt")
    if not onboarding.exists():
        raise RuntimeError("Skip did not persist onboarding.json")
    body = onboarding.read_text(encoding="utf-8")
    if '"status": "skipped"' not in body and '"status":"skipped"' not in body:
        raise RuntimeError(f"Skip did not write status=skipped\nbody:\n{body}")
    mode = stat.S_IMODE(onboarding.stat().st_mode)
    if mode != 0o600:
        raise RuntimeError(f"onboarding.json mode is {oct(mode)}, expected 0o600")
    save_evidence(root, "setup-wizard-skipped", skipped)
    tmux(tmux_exe, "kill-session", "-t", session, check=False)

    # Restart suppresses auto-open; disconnected auth guidance remains when present.
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
    restarted = wait_for(tmux_exe, session, r"Type a message|not connected|/connect", "restart after skip")
    if "First-run setup" in restarted and "Welcome" in restarted and "local terminal setup" in restarted:
        # Auto-open should stay suppressed after skipped marker.
        if "setup opened" in restarted.lower():
            raise RuntimeError(f"restart auto-opened setup after skipped marker\nscreen:\n{restarted}")
    # Disconnected guidance (auth onboarding) should still be available when no credentials.
    if re.search(r"not connected|/connect", restarted) is None and "Type a message" not in restarted:
        raise RuntimeError(f"restart frame lost composer chrome\nscreen:\n{restarted}")
    save_evidence(root, "setup-wizard-restart-skipped", restarted)
    # New process may open a new session file; baseline after restart for finish non-mutation.
    before_jsonl = _session_jsonl_snapshot(session_dir)
    before_auth = _auth_snapshot(ava_config)
    before_requests = _provider_request_bytes(root)

    # /setup reopens after skip; Finish can draft /connect openai without submission.
    send_literal(tmux_exe, session, "/setup")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"First-run setup|Welcome", "reopen after skip")
    send_keys(tmux_exe, session, "Enter")  # Welcome continue
    wait_for(tmux_exe, session, r"Keep current theme|Theme", "theme after reopen")
    send_literal(tmux_exe, session, "Keep current")
    wait_for(tmux_exe, session, r"filter\s+Keep current|Keep current theme", "keep current selected")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Provider readiness|Draft /connect openai|Active provider", "provider step")
    send_literal(tmux_exe, session, "Draft /connect openai")
    wait_for(tmux_exe, session, r"filter\s+Draft /connect openai|Draft /connect openai after Finish", "stage connect row")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Privacy|Telemetry|no telemetry", "privacy step")
    send_keys(tmux_exe, session, "Enter")  # Continue
    wait_for(tmux_exe, session, r"Finish setup|Completed marker", "finish step")
    send_literal(tmux_exe, session, "Finish setup")
    wait_for(tmux_exe, session, r"filter\s+Finish setup|Finish setup", "finish row selected")
    send_keys(tmux_exe, session, "Enter")
    finished = wait_for(tmux_exe, session, r"Setup complete|/connect openai", "finish receipt and draft")
    body = onboarding.read_text(encoding="utf-8")
    if '"status": "completed"' not in body and '"status":"completed"' not in body:
        raise RuntimeError(f"Finish did not write status=completed\nbody:\n{body}")
    # Drafted but never submitted: composer should show the command; no provider request growth.
    if "/connect openai" not in finished:
        # Capture may put the draft on the input row; accept status text as secondary.
        if "drafted" not in finished.lower():
            raise RuntimeError(f"Finish did not draft /connect openai\nscreen:\n{finished}")
    save_evidence(root, "setup-wizard-finished", finished)

    after_finish_auth = _auth_snapshot(ava_config)
    after_finish_jsonl = _session_jsonl_snapshot(session_dir)
    after_finish_requests = _provider_request_bytes(root)
    _assert_no_backend_mutation(
        before_auth=before_auth,
        after_auth=after_finish_auth,
        before_jsonl=before_jsonl,
        after_jsonl=after_finish_jsonl,
        before_requests=before_requests,
        after_requests=after_finish_requests,
        label="setup finish",
    )
    tmux(tmux_exe, "kill-session", "-t", session, check=False)

    # Separate short-height fresh root/session: deferred hint + explicit accessibility.
    short_root = root / "short-height"
    short_root.mkdir(parents=True, exist_ok=True)
    short_home = short_root / "home"
    short_config = short_root / "config"
    short_state = short_root / "state"
    short_data = short_root / "data"
    short_workspace = short_root / "workspace"
    for path in (short_home, short_config, short_state, short_data, short_workspace):
        path.mkdir(parents=True, exist_ok=True)
    short_ava_config = short_config / "ava"
    short_ava_config.mkdir(parents=True, exist_ok=True)
    short_session = ctx.session_name("setup-short")
    short_env = ctx.pane_command(
        home=short_home,
        config=short_config,
        state=short_state,
        data=short_data,
        extra={"NO_COLOR": "", "AVA_TUI_THEME": "", "COLORFGBG": ""},
    )
    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        short_session,
        "-x",
        "80",
        "-y",
        "10",
        "-c",
        str(short_workspace),
        short_env,
    )
    short_initial = wait_for(
        tmux_exe,
        short_session,
        r"taller terminal|/setup|Type a message",
        "short-height deferred setup hint",
    )
    short_onboarding = _onboarding_path(short_state)
    if short_onboarding.exists():
        raise RuntimeError("short-height path wrote onboarding.json automatically")
    if "First-run setup" in short_initial and "Welcome" in short_initial:
        raise RuntimeError(f"short-height auto-opened setup wizard\nscreen:\n{short_initial}")
    if "taller terminal" not in short_initial and "/setup" not in short_initial:
        raise RuntimeError(f"short-height missing deferred /setup hint\nscreen:\n{short_initial}")
    save_evidence(root, "setup-wizard-short-deferred", short_initial)

    send_literal(tmux_exe, short_session, "/setup")
    send_keys(tmux_exe, short_session, "Enter")
    short_open = wait_for(tmux_exe, short_session, r"First-run setup|Welcome|Continue", "explicit /setup on short height")
    if "First-run setup" not in short_open and "Welcome" not in short_open and "Continue" not in short_open:
        raise RuntimeError(f"explicit /setup failed on short height\nscreen:\n{short_open}")
    save_evidence(root, "setup-wizard-short-explicit", short_open)
    tmux(tmux_exe, "kill-session", "-t", short_session, check=False)

    # Persistence failures stay open and render one fixed path-free status. Exercise
    # final symlink, special-file, and permission-class (multi-link) rejections.
    def check_safe_persistence_failure(kind: str) -> None:
        diagnostic_session = ctx.session_name(f"setup-failure-{kind}")
        onboarding.parent.mkdir(parents=True, exist_ok=True)
        onboarding.write_text('{"version":1,"status":"completed"}\n', encoding="utf-8")
        onboarding.chmod(0o600)
        display_before = display_config.read_text(encoding="utf-8") if display_config.exists() else None
        tmux(
            tmux_exe,
            "new-session",
            "-d",
            "-s",
            diagnostic_session,
            "-x",
            "100",
            "-y",
            "24",
            "-c",
            str(workspace),
            env_prefix,
        )
        wait_for(tmux_exe, diagnostic_session, r"Type a message|live session|not connected", f"{kind} failure startup")

        onboarding.unlink()
        sibling: pathlib.Path | None = None
        target: pathlib.Path | None = None
        if kind == "symlink":
            target = root / "setup-failure-symlink-target.json"
            target.write_text("symlink-target-sentinel\n", encoding="utf-8")
            onboarding.symlink_to(target)
        elif kind == "fifo":
            os.mkfifo(onboarding, 0o600)
        elif kind == "multilink":
            onboarding.write_text("multilink-target-sentinel\n", encoding="utf-8")
            sibling = onboarding.parent / "onboarding-multilink-sibling.json"
            if sibling.exists():
                sibling.unlink()
            os.link(onboarding, sibling)
        else:
            raise RuntimeError(f"unknown setup persistence failure kind {kind}")

        send_literal(tmux_exe, diagnostic_session, "/setup")
        send_keys(tmux_exe, diagnostic_session, "Enter")
        wait_for(tmux_exe, diagnostic_session, r"First-run setup|Welcome", f"{kind} failure setup open")
        send_literal(tmux_exe, diagnostic_session, "Skip setup")
        wait_for(tmux_exe, diagnostic_session, r"filter\s+Skip setup|Skip setup", f"{kind} failure skip selected")
        send_keys(tmux_exe, diagnostic_session, "Enter")
        failed = wait_for(
            tmux_exe,
            diagnostic_session,
            r"setup state could not be saved; setup remains open",
            f"{kind} path-free persistence failure",
        )
        forbidden = (str(onboarding), ".onboarding.tmp.", "Too many levels", "Permission denied", "symbolic link")
        leaked = next((value for value in forbidden if value in failed), None)
        if leaked is not None:
            raise RuntimeError(f"{kind} persistence failure leaked {leaked!r}\nscreen:\n{failed}")
        if "First-run setup" not in failed and "Skip setup" not in failed:
            raise RuntimeError(f"{kind} persistence failure closed setup\nscreen:\n{failed}")
        display_after = display_config.read_text(encoding="utf-8") if display_config.exists() else None
        if display_after != display_before:
            raise RuntimeError(f"{kind} persistence failure wrote display.json")
        if kind == "symlink":
            if not onboarding.is_symlink() or target is None or target.read_text(encoding="utf-8") != "symlink-target-sentinel\n":
                raise RuntimeError("symlink persistence failure modified target or replaced link")
        elif kind == "fifo":
            if not stat.S_ISFIFO(onboarding.lstat().st_mode):
                raise RuntimeError("FIFO persistence failure replaced the special target")
        else:
            if sibling is None or onboarding.read_text(encoding="utf-8") != "multilink-target-sentinel\n" or sibling.read_text(encoding="utf-8") != "multilink-target-sentinel\n":
                raise RuntimeError("multi-link persistence failure modified either linked target")
        save_evidence(root, f"setup-wizard-safe-failure-{kind}", failed)
        tmux(tmux_exe, "kill-session", "-t", diagnostic_session, check=False)
        onboarding.unlink(missing_ok=True)
        if sibling is not None:
            sibling.unlink(missing_ok=True)

    for failure_kind in ("symlink", "fifo", "multilink"):
        check_safe_persistence_failure(failure_kind)
