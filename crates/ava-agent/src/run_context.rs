use std::sync::Arc;

use ava_permissions::inspector::InspectionContext;
use ava_types::{ThinkingLevel, TodoState};
use tokio::sync::RwLock;

#[derive(Debug, Clone, Default)]
pub struct AgentRunContext {
    pub provider: Option<String>,
    pub model: Option<String>,
    pub thinking_level: Option<ThinkingLevel>,
    pub auto_compact: Option<bool>,
    pub compaction_threshold: Option<u8>,
    pub compaction_provider: Option<String>,
    pub compaction_model: Option<String>,
    pub todo_state: Option<TodoState>,
    pub permission_context: Option<Arc<RwLock<InspectionContext>>>,
}

impl AgentRunContext {
    pub fn resolved_model_override(&self) -> Option<(String, String)> {
        match (&self.provider, &self.model) {
            (Some(provider), Some(model)) => Some((provider.clone(), model.clone())),
            _ => None,
        }
    }

    pub fn compaction_model_override(&self) -> Option<(String, String)> {
        match (&self.compaction_provider, &self.compaction_model) {
            (Some(provider), Some(model)) => Some((provider.clone(), model.clone())),
            _ => None,
        }
    }
}
