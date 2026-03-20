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
use crate::registry::{ToolRegistry, ToolTier};

/// Register the 6 default tools that are always sent to the LLM.
///
/// A shared [`hashline::HashlineCache`] is created and passed to both the
/// read and edit tools so that hash-anchored edits work across tool calls.
pub fn register_default_tools(registry: &mut ToolRegistry, platform: Arc<dyn Platform>) {
    let hashline_cache = hashline::new_cache();
    registry.register(read::ReadTool::new(
        platform.clone(),
        hashline_cache.clone(),
    ));
    registry.register(write::WriteTool::new(platform.clone()));
    registry.register(edit::EditTool::new(platform.clone(), hashline_cache));
    registry.register(bash::BashTool::new(platform.clone()));
    registry.register(glob::GlobTool::new());
    registry.register(grep::GrepTool::new());
}

/// Register extended tools (available but not sent to the LLM by default).
///
/// These tools are always *executable* — the tier only controls whether their
/// definitions are included in the system prompt sent to the LLM.
///
/// Pass `shared_index` to let the `code_search` tool reuse the pre-built
/// [`ava_codebase::CodebaseIndex`] that `AgentStack` populates in a background
/// task at startup, avoiding an O(n) disk rebuild on every invocation.
pub fn register_extended_tools(
    registry: &mut ToolRegistry,
    platform: Arc<dyn Platform>,
    shared_index: Option<SharedCodebaseIndex>,
) {
    registry.register_with_tier(
        apply_patch::ApplyPatchTool::new(platform.clone()),
        ToolTier::Extended,
    );
    registry.register_with_tier(web_fetch::WebFetchTool::new(), ToolTier::Extended);
    registry.register_with_tier(web_search::WebSearchTool::new(), ToolTier::Extended);
    registry.register_with_tier(
        multiedit::MultiEditTool::new(platform.clone()),
        ToolTier::Extended,
    );
    registry.register_with_tier(
        ast_ops::AstOpsTool::new(platform.clone()),
        ToolTier::Extended,
    );
    registry.register_with_tier(
        lsp_ops::LspOpsTool::new(platform.clone()),
        ToolTier::Extended,
    );
    let code_search_tool = match shared_index {
        Some(idx) => code_search::CodeSearchTool::with_shared_index(idx),
        None => code_search::CodeSearchTool::new(),
    };
    registry.register_with_tier(code_search_tool, ToolTier::Extended);
    registry.register_with_tier(git_read::GitReadTool::new(), ToolTier::Extended);
    registry.register_with_tier(lint::LintTool::new(platform.clone()), ToolTier::Extended);
    registry.register_with_tier(
        test_runner::TestRunnerTool::new(platform.clone()),
        ToolTier::Extended,
    );
}

/// Register all core tools (default + extended). Backwards-compatible alias.
pub fn register_core_tools(registry: &mut ToolRegistry, platform: Arc<dyn Platform>) {
    register_default_tools(registry, platform.clone());
    register_extended_tools(registry, platform, None);
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
