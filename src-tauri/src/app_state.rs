use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use ava_extensions::ExtensionManager;
use ava_permissions::{Action, Pattern, PermissionSystem, Rule};
use ava_tools::browser::{BrowserDriver, BrowserEngine, BrowserError, BrowserResult};
use ava_tools::edit::{EditEngine, EditRequest};
use ava_tools::git::GitTool;
use ava_validator::{CompilationValidator, SyntaxValidator, ValidationPipeline};
use serde_json::{json, Value};

use crate::commands::ToolInfo;

const MCP_BROWSER_MESSAGE: &str =
    "Browser automation requires an MCP server. Configure a Puppeteer or Playwright MCP server in settings.";

pub struct AppState {
    tool_registry: Arc<ToolRegistry>,
    db: Arc<ava_db::Database>,
    memory: Arc<Mutex<ava_memory::MemorySystem>>,
    permissions: PermissionSystem,
    extensions: Arc<Mutex<ExtensionManager>>,
    validator: Arc<ValidationPipeline>,
}

impl AppState {
    pub async fn new(app_data_dir: PathBuf) -> Result<Self, String> {
        std::fs::create_dir_all(&app_data_dir)
            .map_err(|error| format!("failed to create app data directory: {error}"))?;

        let db = ava_db::Database::create_at(app_data_dir.join("ava.db"))
            .await
            .map_err(|error| format!("failed to initialize database: {error}"))?;
        db.run_migrations()
            .await
            .map_err(|error| format!("failed to run database migrations: {error}"))?;

        let memory = ava_memory::MemorySystem::new(app_data_dir.join("memory.db"))
            .map_err(|error| format!("failed to initialize memory store: {error}"))?;

        let permissions = PermissionSystem::load(
            app_data_dir,
            vec![Rule {
                tool: Pattern::Any,
                args: Pattern::Any,
                action: Action::Allow,
            }],
        );

        let validator = ValidationPipeline::new()
            .with_validator(SyntaxValidator)
            .with_validator(CompilationValidator);

        Ok(Self {
            tool_registry: Arc::new(ToolRegistry::new()),
            db: Arc::new(db),
            memory: Arc::new(Mutex::new(memory)),
            permissions,
            extensions: Arc::new(Mutex::new(ExtensionManager::new())),
            validator: Arc::new(validator),
        })
    }

    pub async fn execute_tool(&self, tool: &str, args: Value) -> Value {
        match self.tool_registry.execute_tool(tool, args).await {
            Ok(content) => json!({ "content": content, "is_error": false }),
            Err(error) => json!({ "content": error, "is_error": true }),
        }
    }

    pub async fn agent_run(&self, _goal: &str) -> Value {
        json!({
            "completed": false,
            "message": "Full agent loop not yet wired - use CLI"
        })
    }

    pub fn list_tools(&self) -> Vec<ToolInfo> {
        self.tool_registry
            .list_tools()
            .into_iter()
            .map(|name| ToolInfo {
                description: tool_description(&name).to_string(),
                name,
            })
            .collect()
    }

    pub fn database_status(&self) -> String {
        let _memory_guard = self.memory.lock().ok();
        let _extensions_guard = self.extensions.lock().ok();
        let _permission_probe = self.permissions.evaluate("status", &[]);
        let _validation_probe = self.validator.validate("status");

        if self.db.pool().is_closed() {
            "db-closed".to_string()
        } else {
            "db-ready".to_string()
        }
    }

    pub fn _memory(&self) -> &Arc<Mutex<ava_memory::MemorySystem>> {
        &self.memory
    }

    pub fn _permissions(&self) -> &PermissionSystem {
        &self.permissions
    }

    pub fn _extensions(&self) -> &Arc<Mutex<ExtensionManager>> {
        &self.extensions
    }

    pub fn _validator(&self) -> &Arc<ValidationPipeline> {
        &self.validator
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ToolKind {
    Git,
    Browser,
    Edit,
    Bash,
}

#[derive(Debug)]
pub struct ToolRegistry {
    tools: HashMap<String, ToolKind>,
}

impl ToolRegistry {
    pub fn new() -> Self {
        let mut tools = HashMap::new();
        tools.insert("git".to_string(), ToolKind::Git);
        tools.insert("browser".to_string(), ToolKind::Browser);
        tools.insert("edit".to_string(), ToolKind::Edit);
        tools.insert("bash".to_string(), ToolKind::Bash);
        Self { tools }
    }

    pub fn list_tools(&self) -> Vec<String> {
        let mut names = self.tools.keys().cloned().collect::<Vec<_>>();
        names.sort();
        names
    }

    pub async fn execute_tool(&self, name: &str, args_json: Value) -> Result<String, String> {
        let Some(tool) = self.tools.get(name).copied() else {
            return Err(format!("unknown tool: {name}"));
        };

        match tool {
            ToolKind::Git => execute_git(args_json).await,
            ToolKind::Browser => execute_browser(args_json),
            ToolKind::Edit => execute_edit(args_json),
            ToolKind::Bash => Err("bash tool is not wired in desktop backend yet".to_string()),
        }
    }
}

impl Default for ToolRegistry {
    fn default() -> Self {
        Self::new()
    }
}

async fn execute_git(args_json: Value) -> Result<String, String> {
    let payload = serde_json::to_string(&args_json)
        .map_err(|error| format!("invalid git payload: {error}"))?;

    let result = GitTool::new()
        .run_from_json(&payload)
        .await
        .map_err(|error| error.to_string())?;

    if result.stdout.is_empty() {
        Ok(result.stderr)
    } else {
        Ok(result.stdout)
    }
}

struct MismatchedBrowserDriver;

impl BrowserDriver for MismatchedBrowserDriver {
    fn navigate(&self, _url: &str) -> Result<BrowserResult, BrowserError> {
        Err(BrowserError::Driver(MCP_BROWSER_MESSAGE.to_string()))
    }

    fn click(&self, _selector: &str) -> Result<BrowserResult, BrowserError> {
        Err(BrowserError::Driver(MCP_BROWSER_MESSAGE.to_string()))
    }

    fn type_text(&self, _selector: &str, _text: &str) -> Result<BrowserResult, BrowserError> {
        Err(BrowserError::Driver(MCP_BROWSER_MESSAGE.to_string()))
    }

    fn extract_text(&self, _selector: &str) -> Result<BrowserResult, BrowserError> {
        Err(BrowserError::Driver(MCP_BROWSER_MESSAGE.to_string()))
    }

    fn screenshot(&self, _path: &str) -> Result<BrowserResult, BrowserError> {
        Err(BrowserError::Driver(MCP_BROWSER_MESSAGE.to_string()))
    }
}

fn execute_browser(args_json: Value) -> Result<String, String> {
    let payload = serde_json::to_string(&args_json)
        .map_err(|error| format!("invalid browser payload: {error}"))?;

    let engine = BrowserEngine::new(&MismatchedBrowserDriver);
    let result = engine
        .dispatch_from_json(&payload)
        .map_err(|error| error.to_string())?;
    Ok(result.output)
}

fn execute_edit(args_json: Value) -> Result<String, String> {
    let content = args_json
        .get("content")
        .and_then(Value::as_str)
        .ok_or_else(|| "edit payload missing 'content'".to_string())?;
    let old_text = args_json
        .get("old_text")
        .or_else(|| args_json.get("oldText"))
        .and_then(Value::as_str)
        .ok_or_else(|| "edit payload missing 'old_text'".to_string())?;
    let new_text = args_json
        .get("new_text")
        .or_else(|| args_json.get("newText"))
        .and_then(Value::as_str)
        .ok_or_else(|| "edit payload missing 'new_text'".to_string())?;

    let request = EditRequest::new(content, old_text, new_text);
    let result = EditEngine::new()
        .apply(&request)
        .map_err(|error| error.to_string())?;

    Ok(result.content)
}

fn tool_description(name: &str) -> &'static str {
    match name {
        "git" => "Run git and gh operations via ava-tools",
        "browser" => "Dispatch browser automation actions",
        "edit" => "Apply text replacement strategies",
        "bash" => "Execute shell commands (not yet wired)",
        _ => "Tool",
    }
}

#[cfg(test)]
mod tests {
    use serde_json::json;
    use serde_json::Value;
    use tempfile::tempdir;

    use super::AppState;

    #[tokio::test]
    async fn app_state_returns_structured_results() {
        let dir = tempdir().expect("temp dir should be created");
        let state = AppState::new(dir.path().to_path_buf())
            .await
            .expect("state should initialize");

        let tool_result = state
            .execute_tool(
                "edit",
                json!({ "content": "hello world", "old_text": "world", "new_text": "ava" }),
            )
            .await;
        assert_eq!(tool_result["is_error"], false);

        let session = state.agent_run("Summarize this repo").await;
        assert_eq!(session["completed"], false);
        assert_eq!(
            session["message"],
            Value::String("Full agent loop not yet wired - use CLI".to_string())
        );

        let tools = state.list_tools();
        assert!(!tools.is_empty());
        assert_eq!(state.database_status(), "db-ready");
    }
}
