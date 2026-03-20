pub mod apply_patch;
pub mod ast_ops;
pub mod bash;
pub mod claude_code;
pub mod code_search;
pub mod custom_tool;
pub mod edit;
pub mod git_read;
pub mod glob;
pub mod grep;
pub mod hashline;
pub mod lint;
pub mod lsp_ops;
pub mod multiedit;
pub mod output_fallback;
pub mod path_guard;
pub mod plan;
pub mod question;
pub mod read;
pub mod secret_redaction;
pub mod task;
pub mod test_runner;
pub mod todo;
pub mod web_fetch;
pub mod web_search;
pub mod write;

/// Wrap `value` in POSIX single-quotes, escaping any embedded single-quotes.
///
/// The result is safe to embed verbatim in an `sh -c` command string as a
/// single argument (no word-splitting, no glob expansion, no variable
/// expansion occurs inside single-quotes).  The only byte that requires
/// special handling is `'` itself: the shell quoting is closed, a
/// backslash-escaped `'` is emitted, and the quoting is re-opened.
///
/// # Examples
/// ```
/// use ava_tools::core::shell_single_quote;
/// assert_eq!(shell_single_quote("src/main.rs"), "'src/main.rs'");
/// assert_eq!(shell_single_quote("a'b"), "'a'\\''b'");
/// ```
pub fn shell_single_quote(value: &str) -> String {
    // Close the single-quote, emit a backslash-escaped literal quote, reopen.
    // e.g. "a'b" -> "'a'\\''b'"
    format!("'{}'", value.replace('\'', "'\\''"))
}

use std::sync::Arc;

use ava_platform::Platform;

use crate::core::code_search::SharedCodebaseIndex;
use crate::registry::ToolRegistry;

/// Register the 9 default tools that are always sent to the LLM.
///
/// The default set covers file I/O, search, shell, web, and git — enough for
/// most coding tasks without overwhelming the tool definition list.
///
/// A shared [`hashline::HashlineCache`] is created and passed to both the
/// read and edit tools so that hash-anchored edits work across tool calls.
pub fn register_default_tools(registry: &mut ToolRegistry, platform: Arc<dyn Platform>) {
    let hashline_cache = hashline::new_cache();
    // Core 6: file I/O + search + shell
    registry.register(read::ReadTool::new(
        platform.clone(),
        hashline_cache.clone(),
    ));
    registry.register(write::WriteTool::new(platform.clone()));
    registry.register(edit::EditTool::new(platform.clone(), hashline_cache));
    registry.register(bash::BashTool::new(platform.clone()));
    registry.register(glob::GlobTool::new());
    registry.register(grep::GrepTool::new());
    // +3: web + git
    registry.register(web_fetch::WebFetchTool::new());
    registry.register(web_search::WebSearchTool::new());
    registry.register(git_read::GitReadTool::new());
}

/// Register extended tools.
///
/// Extended tools (apply_patch, multiedit, ast_ops, lsp_ops, code_search,
/// lint, test_runner) are **not** registered by default. They are available as
/// plugins or can be registered manually for advanced use cases.
///
/// This function is intentionally empty — the signature is kept for
/// backwards-compatibility with any external code that calls it.
#[allow(unused_variables)]
pub fn register_extended_tools(
    registry: &mut ToolRegistry,
    platform: Arc<dyn Platform>,
    shared_index: Option<SharedCodebaseIndex>,
) {
    // No-op: extended tools are not registered by default.
    // They will be available as plugins in a future release.
}

/// Register all core tools. Backwards-compatible alias for `register_default_tools`.
pub fn register_core_tools(registry: &mut ToolRegistry, platform: Arc<dyn Platform>) {
    register_default_tools(registry, platform);
}

/// Register the task tool with a spawner that can create sub-agent runs.
pub fn register_task_tool(registry: &mut ToolRegistry, spawner: Arc<dyn task::TaskSpawner>) {
    registry.register(task::TaskTool::new(spawner));
}

/// Register todo_write and todo_read tools with shared state.
///
/// Separated from `register_core_tools` because these tools require a shared
/// [`ava_types::TodoState`] that the TUI also holds for display.
pub fn register_todo_tools(registry: &mut ToolRegistry, state: ava_types::TodoState) {
    registry.register(todo::TodoWriteTool::new(state.clone()));
    registry.register(todo::TodoReadTool::new(state));
}

/// Register the question tool with a bridge for agent-to-TUI communication.
pub fn register_question_tool(registry: &mut ToolRegistry, bridge: question::QuestionBridge) {
    registry.register(question::QuestionTool::new(bridge));
}

/// Register the plan tool with a bridge for agent-to-TUI communication and shared state.
pub fn register_plan_tool(
    registry: &mut ToolRegistry,
    bridge: plan::PlanBridge,
    state: ava_types::PlanState,
) {
    registry.register(plan::PlanTool::new(bridge, state));
}

/// Register the claude_code tool with the given configuration.
///
/// Always registers the tool; `execute()` returns a clear error if the
/// `claude` binary is not installed.
pub fn register_claude_code_tool(
    registry: &mut ToolRegistry,
    config: ava_config::ClaudeCodeConfig,
) {
    registry.register(claude_code::ClaudeCodeTool::new(config));
}

pub fn register_custom_tools(registry: &mut ToolRegistry, dirs: &[std::path::PathBuf]) {
    custom_tool::register_custom_tools(registry, dirs);
}
