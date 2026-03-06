pub mod bash;
pub mod edit;
pub mod glob;
pub mod grep;
pub mod read;
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
}
