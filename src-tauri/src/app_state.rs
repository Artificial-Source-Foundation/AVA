use std::sync::Arc;

use serde_json::Value;
use tokio::sync::Mutex;

use crate::commands::{MockAgent, MockToolRegistry, ToolInfo};

pub struct AppState {
    tool_registry: Arc<MockToolRegistry>,
    agent: Arc<Mutex<MockAgent>>,
    db: MockDatabase,
}

impl AppState {
    pub fn new() -> Self {
        Self {
            tool_registry: Arc::new(MockToolRegistry::new()),
            agent: Arc::new(Mutex::new(MockAgent::new())),
            db: MockDatabase::new(),
        }
    }

    pub async fn execute_tool(&self, tool: &str, args: Value) -> Value {
        self.tool_registry.execute_tool(tool, args).await
    }

    pub async fn agent_run(&self, goal: &str) -> Value {
        let mut agent = self.agent.lock().await;
        agent.run(goal).await
    }

    pub fn list_tools(&self) -> Vec<ToolInfo> {
        self.tool_registry.list_tools()
    }

    pub fn database_status(&self) -> String {
        self.db.status_message()
    }
}

pub struct MockDatabase;

impl MockDatabase {
    pub fn new() -> Self {
        Self
    }

    pub fn status_message(&self) -> String {
        "mock-db-ready".to_string()
    }
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::AppState;

    #[tokio::test]
    async fn app_state_mock_methods_return_expected_shapes() {
        let state = AppState::new();

        let tool_result = state
            .execute_tool("read_file", json!({ "path": "README.md" }))
            .await;
        assert_eq!(tool_result["is_error"], false);

        let session = state.agent_run("Summarize this repo").await;
        assert_eq!(session["completed"], true);

        let tools = state.list_tools();
        assert!(!tools.is_empty());
        assert_eq!(state.database_status(), "mock-db-ready");
    }
}
