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
pub mod lsp_ops;
pub mod multiedit;
pub mod question;
pub mod read;
pub mod task;
pub mod todo;
pub mod web_fetch;
pub mod web_search;
pub mod write;

use std::sync::Arc;

use ava_platform::Platform;

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
pub fn register_extended_tools(registry: &mut ToolRegistry, platform: Arc<dyn Platform>) {
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
    registry.register_with_tier(code_search::CodeSearchTool::new(), ToolTier::Extended);
    registry.register_with_tier(git_read::GitReadTool::new(), ToolTier::Extended);
}

/// Register all core tools (default + extended). Backwards-compatible alias.
pub fn register_core_tools(registry: &mut ToolRegistry, platform: Arc<dyn Platform>) {
    register_default_tools(registry, platform.clone());
    register_extended_tools(registry, platform);
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
