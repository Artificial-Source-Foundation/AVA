"""Independent tmux TUI smoke scenarios."""

from .suspend_resume import scenario_suspend_resume
from .keybind_conflict import scenario_keybind_conflict
from .theme_env import scenario_theme_env
from .theme_persisted import scenario_theme_persisted
from .nested_settings_preview import scenario_nested_settings_preview
from .startup_overview import scenario_startup_overview
from .active_run import scenario_active_run
from .restore_followup import scenario_restore_followup
from .streaming_scroll import scenario_streaming_scroll
from .mermaid import scenario_mermaid
from .transcript_search import scenario_transcript_search
from .transcript_selection import scenario_transcript_selection
from .subagent_workspace import scenario_subagent_workspace
from .branch_summary import scenario_branch_summary
from .main_startup_trust_keybinds import scenario_main_startup_trust_keybinds
from .main_models_selectors import scenario_main_models_selectors
from .main_editor_input import scenario_main_editor_input
from .main_slash_completions import scenario_main_slash_completions
from .main_permission_flow import scenario_main_permission_flow
from .main_question_flow import scenario_main_question_flow
from .main_session_mgmt import scenario_main_session_mgmt
from .main_paste_scrollback_attach import scenario_main_paste_scrollback_attach
from .plugin_ui import scenario_plugin_ui


SCENARIOS = (
    "suspend_resume",
    "keybind_conflict",
    "theme_env",
    "theme_persisted",
    "nested_settings_preview",
    "startup_overview",
    "active_run",
    "restore_followup",
    "streaming_scroll",
    "mermaid",
    "transcript_search",
    "transcript_selection",
    "subagent_workspace",
    "branch_summary",
    "main_startup_trust_keybinds",
    "main_models_selectors",
    "main_editor_input",
    "main_slash_completions",
    "main_permission_flow",
    "main_question_flow",
    "main_session_mgmt",
    "main_paste_scrollback_attach",
    "plugin_ui",
)

SCENARIO_HANDLERS = {
    "suspend_resume": scenario_suspend_resume,
    "keybind_conflict": scenario_keybind_conflict,
    "theme_env": scenario_theme_env,
    "theme_persisted": scenario_theme_persisted,
    "nested_settings_preview": scenario_nested_settings_preview,
    "startup_overview": scenario_startup_overview,
    "active_run": scenario_active_run,
    "restore_followup": scenario_restore_followup,
    "streaming_scroll": scenario_streaming_scroll,
    "mermaid": scenario_mermaid,
    "transcript_search": scenario_transcript_search,
    "transcript_selection": scenario_transcript_selection,
    "subagent_workspace": scenario_subagent_workspace,
    "branch_summary": scenario_branch_summary,
    "main_startup_trust_keybinds": scenario_main_startup_trust_keybinds,
    "main_models_selectors": scenario_main_models_selectors,
    "main_editor_input": scenario_main_editor_input,
    "main_slash_completions": scenario_main_slash_completions,
    "main_permission_flow": scenario_main_permission_flow,
    "main_question_flow": scenario_main_question_flow,
    "main_session_mgmt": scenario_main_session_mgmt,
    "main_paste_scrollback_attach": scenario_main_paste_scrollback_attach,
    "plugin_ui": scenario_plugin_ui,
}
