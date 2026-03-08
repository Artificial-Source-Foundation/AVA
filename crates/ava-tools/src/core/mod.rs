pub mod apply_patch;
pub mod bash;
pub mod codebase_search;
pub mod custom_tool;
pub mod diagnostics;
pub mod edit;
pub mod git_read;
pub mod glob;
pub mod grep;
pub mod lint;
pub mod memory;
pub mod multiedit;
pub mod read;
pub mod session_ops;
pub mod session_search;
pub mod test_runner;
pub mod write;

use std::sync::Arc;

use ava_codebase::CodebaseIndex;
use ava_memory::MemorySystem;
use ava_platform::Platform;
use ava_session::SessionManager;
use tokio::sync::RwLock;

use crate::registry::ToolRegistry;

pub fn register_core_tools(registry: &mut ToolRegistry, platform: Arc<dyn Platform>) {
    registry.register(read::ReadTool::new(platform.clone()));
    registry.register(write::WriteTool::new(platform.clone()));
    registry.register(edit::EditTool::new(platform.clone()));
    registry.register(bash::BashTool::new(platform.clone()));
    registry.register(glob::GlobTool::new());
    registry.register(grep::GrepTool::new());
    registry.register(multiedit::MultiEditTool::new(platform.clone()));
    registry.register(apply_patch::ApplyPatchTool::new(platform.clone()));
    registry.register(test_runner::TestRunnerTool::new(platform.clone()));
    registry.register(lint::LintTool::new(platform.clone()));
    registry.register(diagnostics::DiagnosticsTool::new(platform.clone()));
}

pub fn register_memory_tools(registry: &mut ToolRegistry, memory: Arc<MemorySystem>) {
    registry.register(memory::RememberTool::new(memory.clone()));
    registry.register(memory::RecallTool::new(memory.clone()));
    registry.register(memory::MemorySearchTool::new(memory));
}

pub fn register_codebase_tools(
    registry: &mut ToolRegistry,
    index: Arc<RwLock<Option<Arc<CodebaseIndex>>>>,
) {
    registry.register(codebase_search::CodebaseSearchTool::new(index));
}

pub fn register_custom_tools(registry: &mut ToolRegistry, dirs: &[std::path::PathBuf]) {
    custom_tool::register_custom_tools(registry, dirs);
}

pub fn register_session_tools(registry: &mut ToolRegistry, session_manager: Arc<SessionManager>) {
    registry.register(session_search::SessionSearchTool::new(
        session_manager.clone(),
    ));
    registry.register(session_ops::SessionListTool::new(session_manager.clone()));
    registry.register(session_ops::SessionLoadTool::new(session_manager));
}
