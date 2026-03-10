pub mod apply_patch;
pub mod bash;
// codebase_search: source retained but not compiled (crate dep removed)
// pub mod codebase_search;
pub mod custom_tool;
pub mod diagnostics;
pub mod edit;
pub mod git_read;
pub mod glob;
pub mod grep;
pub mod lint;
// memory: source retained but not compiled (crate dep removed)
// pub mod memory;
pub mod multiedit;
pub mod question;
pub mod read;
// session_ops: source retained but not compiled (crate dep removed)
// pub mod session_ops;
// session_search: source retained but not compiled (crate dep removed)
// pub mod session_search;
pub mod task;
pub mod test_runner;
pub mod todo;
pub mod web_fetch;
pub mod write;

use std::sync::Arc;

use ava_platform::Platform;

use crate::registry::ToolRegistry;

pub fn register_core_tools(registry: &mut ToolRegistry, platform: Arc<dyn Platform>) {
    registry.register(read::ReadTool::new(platform.clone()));
    registry.register(write::WriteTool::new(platform.clone()));
    registry.register(edit::EditTool::new(platform.clone()));
    registry.register(bash::BashTool::new(platform.clone()));
    registry.register(glob::GlobTool::new());
    registry.register(grep::GrepTool::new());
    registry.register(apply_patch::ApplyPatchTool::new(platform.clone()));
    registry.register(web_fetch::WebFetchTool::new());
}

/// Register the task tool with a spawner that can create sub-agent runs.
pub fn register_task_tool(
    registry: &mut ToolRegistry,
    spawner: Arc<dyn task::TaskSpawner>,
) {
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
pub fn register_question_tool(
    registry: &mut ToolRegistry,
    bridge: question::QuestionBridge,
) {
    registry.register(question::QuestionTool::new(bridge));
}

pub fn register_custom_tools(registry: &mut ToolRegistry, dirs: &[std::path::PathBuf]) {
    custom_tool::register_custom_tools(registry, dirs);
}
