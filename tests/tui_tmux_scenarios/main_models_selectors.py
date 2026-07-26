"""The tmux TUI smoke scenario for main models selectors."""

from __future__ import annotations

import json

from tui_smoke_helpers import (
    SmokeContext,
    capture,
    save_evidence,
    selected_modal_identity,
    selected_modal_row,
    send_keys,
    send_literal,
    tmux,
    wait_for,
    wait_for_absent,
    wait_for_json_file,
    wait_for_screen_change,
    wait_for_selected_modal_change,
    wait_for_session_exit,
)
from .common import _finish_main, _main_session


def scenario_main_models_selectors(ctx: SmokeContext) -> None:
    tmux_exe, root, workspace, ava_config, env_prefix, session = _main_session(ctx)
    scoped_persist_session = ctx.session_name("scoped-persist")
    send_keys(tmux_exe, session, "C-p")
    model_cycle = wait_for(
        tmux_exe,
        session,
        r"model cycled|GPT-5\.6 Sol|gpt-5\.6-sol|GPT-4\.1 mini|gpt-4\.1-mini",
        "ctrl-p model cycle",
    )
    if not any(value in model_cycle for value in ("model cycled", "GPT-5.6 Sol", "gpt-5.6-sol", "GPT-4.1 mini", "gpt-4.1-mini")):
        raise RuntimeError(f"Ctrl+P did not cycle the visible model state\nscreen:\n{model_cycle}")
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "82", "-y", "10")
    wait_for(tmux_exe, session, r"Type a message|live session", "compact frame before selector navigation")
    send_literal(tmux_exe, session, "/models")
    wait_for(tmux_exe, session, r"/models", "exact models selector draft")
    send_keys(tmux_exe, session, "Enter")
    command_model_selector = wait_for(tmux_exe, session, r"Select model|Search models", "exact models selector")
    if "Select model" not in command_model_selector and "Search models" not in command_model_selector:
        raise RuntimeError(f"Exact /models did not bypass autocomplete and open the model selector\nscreen:\n{command_model_selector}")
    selected_row = selected_modal_row(command_model_selector)
    if not selected_row:
        raise RuntimeError(f"Model selector did not expose a selected row before navigation\nscreen:\n{command_model_selector}")
    selected_rows = {selected_modal_identity(selected_row)}
    initial_model_selector = command_model_selector
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "82", "-y", "11")
    resized_model_selector = wait_for(tmux_exe, session, r"Select model|Search models", "resized compact model selector")
    resized_selected_row = selected_modal_row(resized_model_selector)
    if not resized_selected_row or selected_modal_identity(resized_selected_row) != selected_modal_identity(selected_row):
        raise RuntimeError(
            "Model selector lost its selected row while resizing between compact terminal heights\n"
            f"before:\n{command_model_selector}\nafter:\n{resized_model_selector}"
        )
    selected_row = resized_selected_row
    send_keys(tmux_exe, session, "Down")
    selected_row, _ = wait_for_selected_modal_change(
        tmux_exe, session, selected_row, "tmux Down arrow model navigation"
    )
    selected_rows.add(selected_modal_identity(selected_row))
    send_literal(tmux_exe, session, "\x1b[1;129B")
    selected_row, _ = wait_for_selected_modal_change(
        tmux_exe, session, selected_row, "physical Ghostty CSI arrow model navigation with Num Lock"
    )
    selected_rows.add(selected_modal_identity(selected_row))
    for step in range(7):
        send_keys(tmux_exe, session, "Down")
        selected_row, _ = wait_for_selected_modal_change(
            tmux_exe, session, selected_row, f"model selector navigation step {step + 3}"
        )
        selected_rows.add(selected_modal_identity(selected_row))
    if len(selected_rows) < 9:
        raise RuntimeError(
            "Arrow navigation did not visit nine distinct model rows\n"
            f"visited: {sorted(selected_rows)}\nscreen:\n{capture(tmux_exe, session)}"
        )
    if not any(identity and identity not in initial_model_selector for identity in selected_rows):
        raise RuntimeError(
            "Model selector selection never advanced beyond the initial compact viewport\n"
            f"visited: {sorted(selected_rows)}\ninitial screen:\n{initial_model_selector}\n"
            f"final screen:\n{capture(tmux_exe, session)}"
        )
    save_evidence(root, "model-selector-arrow-scroll", capture(tmux_exe, session))
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Select model|Search models", "exact models selector canceled")
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "82", "-y", "10")
    wait_for(tmux_exe, session, r"Type a message|live session", "compact frame before provider modal navigation")
    send_literal(tmux_exe, session, "/connect")
    wait_for(tmux_exe, session, r"/connect", "provider modal command draft")
    send_keys(tmux_exe, session, "Enter")
    provider_modal = wait_for(tmux_exe, session, r"Connect a provider|Select provider", "provider question modal")
    provider_selected_row = selected_modal_row(provider_modal)
    if not provider_selected_row:
        raise RuntimeError(f"Provider question modal did not expose a selected row\nscreen:\n{provider_modal}")
    send_literal(tmux_exe, session, "\x1b[1;129B")
    _, provider_modal_after_arrow = wait_for_selected_modal_change(
        tmux_exe, session, provider_selected_row, "provider modal physical Ghostty arrow navigation with Num Lock"
    )
    save_evidence(root, "provider-modal-arrow-navigation", provider_modal_after_arrow)
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Connect a provider|Select provider", "provider question modal canceled")
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "120", "-y", "32")
    wait_for(tmux_exe, session, r"Type a message|live session", "restored frame after compact modal navigation")
    send_keys(tmux_exe, session, "C-l")
    model_selector = wait_for(tmux_exe, session, r"Select model|Search models", "ctrl-l model selector")
    if "Select model" not in model_selector and "Search models" not in model_selector:
        raise RuntimeError(f"Ctrl+L did not open the model selector\nscreen:\n{model_selector}")
    send_literal(tmux_exe, session, "Diagnostic")
    diagnostic_model_selector = wait_for(
        tmux_exe, session, r"(?s)filter\s+Diagnostic█.*›\s+Diagnostic Local", "quiet filtered model selector"
    )
    if (
        "Diagnostic Local" not in diagnostic_model_selector
        or "diagnostics" in diagnostic_model_selector
        or "reasoning" in diagnostic_model_selector
        or "tools yes" in diagnostic_model_selector
        or "openai/diagnostic-local" in diagnostic_model_selector
    ):
        raise RuntimeError(
            f"Model selector did not keep the custom model row quiet and human-readable\nscreen:\n{diagnostic_model_selector}"
        )
    save_evidence(root, "model-selector-quiet-filtered", diagnostic_model_selector)
    model_before_short_resize = capture(tmux_exe, session)
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "100", "-y", "12")
    wait_for_screen_change(tmux_exe, session, model_before_short_resize, "100x12 model selector resize")
    diagnostic_model_short = wait_for(
        tmux_exe, session, r"(?s)filter\s+Diagnostic█.*›\s+Diagnostic Local", "100x12 quiet model selector"
    )
    if tmux(tmux_exe, "display-message", "-p", "-t", session, "#{window_width},#{window_height}").stdout.strip() != "100,12":
        raise RuntimeError("short model selector did not settle at 100x12")
    if "diagnostics" in diagnostic_model_short or "openai/diagnostic-local" in diagnostic_model_short:
        raise RuntimeError(f"100x12 model selector exposed backend metadata\nscreen:\n{diagnostic_model_short}")
    save_evidence(root, "model-selector-quiet-100x12", diagnostic_model_short)
    tmux(tmux_exe, "resize-window", "-t", session, "-x", "120", "-y", "32")
    wait_for(tmux_exe, session, r"(?s)filter\s+Diagnostic█.*›\s+Diagnostic Local", "restored quiet model selector")
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Select model|Search models", "model selector canceled")
    send_literal(tmux_exe, session, "/scoped-models")
    wait_for(tmux_exe, session, r"/scoped-models", "scoped model selector draft")
    send_keys(tmux_exe, session, "Enter")
    scoped_model_selector = wait_for(
        tmux_exe, session, r"Scoped model cycle|Search models", "scoped model selector"
    )
    if "Scoped model cycle" not in scoped_model_selector:
        raise RuntimeError(f"/scoped-models did not open the scoped cycle selector\nscreen:\n{scoped_model_selector}")
    send_literal(tmux_exe, session, "Diagnostic")
    wait_for(tmux_exe, session, r"Diagnostic Local", "scoped model reorder filtered row")
    send_keys(tmux_exe, session, "M-Up")
    send_keys(tmux_exe, session, *("BSpace" for _ in "Diagnostic"))
    wait_for(tmux_exe, session, r"filter\s+Search models", "scoped model reorder filter clear acknowledgement")
    send_keys(tmux_exe, session, "C-s")
    saved_reordered_models = wait_for_json_file(
        ava_config / "models.json",
        lambda value: isinstance(value, dict)
        and isinstance(value.get("scoped_model_cycle"), list)
        and len(value["scoped_model_cycle"]) >= 2
        and "openai/diagnostic-local" in value["scoped_model_cycle"],
        "persisted reordered scoped model cycle",
    )
    saved_reordered_cycle = json.loads(saved_reordered_models).get("scoped_model_cycle")
    if (
        not isinstance(saved_reordered_cycle, list)
        or len(saved_reordered_cycle) < 2
        or "openai/diagnostic-local" not in saved_reordered_cycle
    ):
        raise RuntimeError(
            "Alt+Up did not make the scoped model cycle explicit before saving\n"
            f"content:\n{saved_reordered_models}"
        )
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Scoped model cycle|Search models", "scoped model reorder selector canceled")
    send_literal(tmux_exe, session, "/scoped-models")
    wait_for(tmux_exe, session, r"/scoped-models", "scoped model selector draft after reorder")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Scoped model cycle|Search models", "scoped model selector after reorder")
    send_keys(tmux_exe, session, "C-x")
    scoped_model_cleared = wait_for(
        tmux_exe, session, r"0 of [0-9]+ enabled|disabled", "scoped model selector clear visible"
    )
    if "0 of " not in scoped_model_cleared or "disabled" not in scoped_model_cleared:
        raise RuntimeError(
            f"Ctrl+X did not clear the visible scoped model cycle\nscreen:\n{scoped_model_cleared}"
        )
    send_keys(tmux_exe, session, "C-s")
    saved_empty_models = wait_for_json_file(
        ava_config / "models.json",
        lambda value: isinstance(value, dict)
        and value.get("scoped_model_cycle") == []
        and any(model.get("name") == "Diagnostic Local" for model in value.get("models", []) if isinstance(model, dict)),
        "persisted empty scoped model cycle",
    )
    if '"scoped_model_cycle": []' not in saved_empty_models or "Diagnostic Local" not in saved_empty_models:
        raise RuntimeError(
            f"Ctrl+S did not persist the empty scoped model cycle while preserving custom models\ncontent:\n{saved_empty_models}"
        )
    tmux(
        tmux_exe,
        "new-session",
        "-d",
        "-s",
        scoped_persist_session,
        "-x",
        "100",
        "-y",
        "24",
        "-c",
        str(workspace),
        env_prefix,
    )
    wait_for(tmux_exe, scoped_persist_session, r"Type a message|live session", "scoped model restart frame")
    send_keys(tmux_exe, scoped_persist_session, "C-p")
    scoped_model_cycle_restart_empty = wait_for(
        tmux_exe,
        scoped_persist_session,
        r"enabled for cycling|no registered provider models",
        "persisted empty scoped model cycle status",
    )
    if (
        "enabled for cycling" not in scoped_model_cycle_restart_empty
        and "no registered provider models" not in scoped_model_cycle_restart_empty
    ):
        raise RuntimeError(
            "A fresh TUI did not load the persisted empty scoped model cycle\n"
            f"screen:\n{scoped_model_cycle_restart_empty}"
        )
    send_keys(tmux_exe, scoped_persist_session, "C-d")
    wait_for_session_exit(tmux_exe, scoped_persist_session)
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Scoped model cycle|Search models", "scoped model selector canceled")
    send_keys(tmux_exe, session, "C-p")
    scoped_model_cycle_empty = wait_for(
        tmux_exe, session, r"enabled for cycling|no registered provider models", "empty scoped model cycle status"
    )
    if "enabled for cycling" not in scoped_model_cycle_empty and "no registered provider models" not in scoped_model_cycle_empty:
        raise RuntimeError(
            f"Ctrl+P did not report the empty scoped model cycle\nscreen:\n{scoped_model_cycle_empty}"
        )
    send_literal(tmux_exe, session, "/scoped-models")
    wait_for(tmux_exe, session, r"/scoped-models", "scoped model selector restore draft")
    send_keys(tmux_exe, session, "Enter")
    wait_for(tmux_exe, session, r"Scoped model cycle|Search models", "scoped model selector restore")
    send_keys(tmux_exe, session, "C-a")
    scoped_model_enabled = wait_for(
        tmux_exe, session, r"All registered models enabled", "scoped model selector enable visible"
    )
    if "All registered models enabled" not in scoped_model_enabled:
        raise RuntimeError(
            f"Ctrl+A did not restore the visible scoped model cycle\nscreen:\n{scoped_model_enabled}"
    )
    send_keys(tmux_exe, session, "C-s")
    saved_all_models = wait_for_json_file(
        ava_config / "models.json",
        lambda value: isinstance(value, dict)
        and "scoped_model_cycle" not in value
        and any(model.get("name") == "Diagnostic Local" for model in value.get("models", []) if isinstance(model, dict)),
        "persisted all-model scoped cycle",
    )
    if "scoped_model_cycle" in saved_all_models or "Diagnostic Local" not in saved_all_models:
        raise RuntimeError(
            f"Ctrl+S did not remove the scoped model cycle field while preserving custom models\ncontent:\n{saved_all_models}"
        )
    send_keys(tmux_exe, session, "Escape")
    wait_for_absent(tmux_exe, session, r"Scoped model cycle|Search models", "scoped model selector restore canceled")
    send_keys(tmux_exe, session, "C-p")
    restored_model_cycle = wait_for(
        tmux_exe,
        session,
        r"model cycled|GPT-5\.6 (?:Sol|Terra|Luna)|gpt-5\.6-(?:sol|terra|luna)|GPT-4\.1 mini|gpt-4\.1-mini|Claude Sonnet 4\.5|claude-sonnet-4-5",
        "restored scoped model cycle",
    )
    restored_cycle_markers = (
        "model cycled",
        "GPT-5.6 Sol",
        "GPT-5.6 Terra",
        "GPT-5.6 Luna",
        "gpt-5.6-sol",
        "gpt-5.6-terra",
        "gpt-5.6-luna",
        "GPT-4.1 mini",
        "gpt-4.1-mini",
        "Claude Sonnet 4.5",
        "claude-sonnet-4-5",
    )
    if not any(value in restored_model_cycle for value in restored_cycle_markers):
        raise RuntimeError(f"Ctrl+P did not cycle after restoring scoped models\nscreen:\n{restored_model_cycle}")

    _finish_main(tmux_exe, session)
