# Sprint 36: Tauri Integration

**Epic:** Frontend Integration (Epic 5)  
**Duration:** 2 weeks  
**Goal:** Tauri commands, event streaming, state management

## Stories

### Story 5.1: Tauri Commands
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
`src-tauri/src/commands.rs`:

```rust
use tauri::State;
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct AppState {
    agent: Arc<Mutex<AgentLoop>>,
    tool_registry: Arc<ToolRegistry>,
    db: Database,
}

#[tauri::command]
async fn execute_tool(
    tool: String,
    args: serde_json::Value,
    state: State<'_, AppState>
) -> Result<serde_json::Value, String> {
    let registry = state.tool_registry.clone();
    let call = ToolCall { name: tool, arguments: args };
    
    match registry.execute(call).await {
        Ok(result) => Ok(serde_json::json!({
            "content": result.content,
            "is_error": result.is_error
        })),
        Err(e) => Err(e.to_string()),
    }
}

#[tauri::command]
async fn agent_run(
    goal: String,
    state: State<'_, AppState>
) -> Result<Session, String> {
    let mut agent = state.agent.lock().await;
    agent.run(&goal).await.map_err(|e| e.to_string())
}

#[tauri::command]
async fn agent_stream(
    goal: String,
    window: tauri::Window,
    state: State<'_, AppState>
) -> Result<(), String> {
    let mut agent = state.agent.lock().await;
    let mut stream = agent.run_streaming(&goal).await;
    
    while let Some(event) = stream.next().await {
        window.emit("agent-event", event).map_err(|e| e.to_string())?;
    }
    
    Ok(())
}

#[tauri::command]
async fn list_tools(
    state: State<'_, AppState>
) -> Result<Vec<ToolInfo>, String> {
    let tools = state.tool_registry.list_tools();
    Ok(tools.iter().map(|t| ToolInfo {
        name: t.name().to_string(),
        description: t.description().to_string(),
    }).collect())
}
```

**Acceptance Criteria:**
- [ ] Commands exposed to frontend
- [ ] Tool execution works
- [ ] Agent loop accessible

---

### Story 5.2: Event Streaming
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
Event streaming to frontend:

```rust
// Stream types
#[derive(Clone, Serialize)]
#[serde(tag = "type")]
pub enum AgentEvent {
    Token { content: String },
    ToolCall { name: String, args: Value },
    ToolResult { content: String, is_error: bool },
    Progress { message: String },
    Complete { session: Session },
    Error { message: String },
}

// Frontend listens:
// window.listen('agent-event', (event) => {
//   handleEvent(event.payload);
// });
```

**Acceptance Criteria:**
- [ ] Real-time streaming works
- [ ] Frontend receives events
- [ ] Progress visible

---

### Story 5.3: State Management
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to build:**
Tauri state:

```rust
pub fn setup_app() -> Result<()> {
    tauri::Builder::default()
        .manage(AppState {
            agent: Arc::new(Mutex::new(AgentLoop::new())),
            tool_registry: Arc::new(ToolRegistry::new()),
            db: Database::new()?, 
        })
        .invoke_handler(tauri::generate_handler![
            execute_tool,
            agent_run,
            agent_stream,
            list_tools,
            // ... more commands
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
        
    Ok(())
}
```

**Acceptance Criteria:**
- [ ] State persists
- [ ] Shared between commands
- [ ] Thread-safe

---

## Sprint Goal

**Success Criteria:**
- [ ] Rust backend accessible from TS
- [ ] Events stream to frontend
- [ ] State managed properly

**Next:** Sprint 37 - Frontend Updates
