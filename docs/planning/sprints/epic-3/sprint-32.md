# Sprint 32: MCP & Integration

**Epic:** Agent Core (Epic 3)  
**Duration:** 2 weeks  
**Goal:** MCP client/server, session management

## Stories

### Story 3.7: MCP Client
**Points:** 16 (AI: 8 hrs, Human: 8 hrs)

**What to build:**
MCP client in `crates/ava-mcp/src/client.rs`

```rust
pub struct MCPClient {
    servers: HashMap<String, MCPServer>,
}

pub struct MCPServer {
    name: String,
    process: Child,
    transport: Transport,
    tools: Vec<Tool>,
}

impl MCPClient {
    pub async fn connect(&mut self, config: ServerConfig) -> Result<()> {
        // Spawn MCP server process
        let mut process = Command::new(&config.command)
            .args(&config.args)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()?;
            
        // Initialize transport
        let transport = Transport::new(process.stdin.take(), process.stdout.take());
        
        // Initialize handshake
        transport.send(json!({
            "jsonrpc": "2.0",
            "method": "initialize",
            "params": { /* ... */ }
        })).await?;
        
        let response = transport.receive().await?;
        
        // List tools
        transport.send(json!({
            "jsonrpc": "2.0",
            "method": "tools/list"
        })).await?;
        
        let tools_response = transport.receive().await?;
        let tools = parse_tools(&tools_response)?;
        
        let server = MCPServer {
            name: config.name,
            process,
            transport,
            tools,
        };
        
        self.servers.insert(config.name, server);
        Ok(())
    }
    
    pub async fn call_tool(&self, server: &str, tool: &str, args: Value) -> Result<ToolResult> {
        let server = self.servers.get(server)
            .ok_or(Error::ServerNotFound)?;
            
        server.transport.send(json!({
            "jsonrpc": "2.0",
            "method": "tools/call",
            "params": {
                "name": tool,
                "arguments": args
            }
        })).await?;
        
        let response = server.transport.receive().await?;
        parse_tool_result(response)
    }
    
    pub fn list_all_tools(&self) -> Vec<(&str, &Tool)> {
        // Aggregate tools from all servers
    }
}
```

**Acceptance Criteria:**
- [ ] Connect to MCP servers
- [ ] Discover tools
- [ ] Call MCP tools

---

### Story 3.8: MCP Server Mode
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
MCP server in `crates/ava-mcp/src/server.rs`

```rust
pub struct AVAMCPServer {
    tool_registry: Arc<ToolRegistry>,
}

impl AVAMCPServer {
    pub fn new(registry: Arc<ToolRegistry>) -> Self {
        Self { tool_registry: registry }
    }
    
    pub async fn run(self) -> Result<()> {
        let stdin = tokio::io::stdin();
        let stdout = tokio::io::stdout();
        
        // Handle MCP protocol
        loop {
            let request = read_request(&mut stdin).await?;
            
            let response = match request.method.as_str() {
                "initialize" => self.handle_initialize(request),
                "tools/list" => self.handle_list_tools(),
                "tools/call" => self.handle_call_tool(request).await,
                _ => json!({"error": "Unknown method"}),
            };
            
            write_response(&mut stdout, response).await?;
        }
    }
    
    fn handle_list_tools(&self) -> Value {
        let tools: Vec<_> = self.tool_registry.list_tools()
            .iter()
            .map(|t| json!({
                "name": t.name(),
                "description": t.description(),
                "inputSchema": t.parameters()
            }))
            .collect();
            
        json!({ "tools": tools })
    }
    
    async fn handle_call_tool(&self, request: Request) -> Value {
        let params = request.params;
        let name = params["name"].as_str().unwrap();
        let args = params["arguments"].clone();
        
        let call = ToolCall {
            name: name.to_string(),
            arguments: args,
        };
        
        match self.tool_registry.execute(call).await {
            Ok(result) => json!({ "content": [{"type": "text", "text": result.content }] }),
            Err(e) => json!({ "error": e.to_string() }),
        }
    }
}
```

**Competitor Reference:** Zed's MCP server mode

**Acceptance Criteria:**
- [ ] AVA exposes tools via MCP
- [ ] Other agents can call AVA
- [ ] Protocol compliant

---

### Story 3.9: Session Management
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
Session management in `crates/ava-session/src/lib.rs`

```rust
pub struct SessionManager {
    db: Database,
}

impl SessionManager {
    pub async fn create(&self) -> Result<Session> {
        let session = Session {
            id: Uuid::new_v4(),
            created_at: Utc::now(),
            updated_at: Utc::now(),
            messages: vec![],
            parent: None,
            children: vec![],
        };
        
        self.db.save_session(&session).await?;
        Ok(session)
    }
    
    pub async fn fork(&self, session: &Session) -> Result<Session> {
        let fork = Session {
            id: Uuid::new_v4(),
            created_at: Utc::now(),
            updated_at: Utc::now(),
            messages: session.messages.clone(),
            parent: Some(session.id),
            children: vec![],
        };
        
        self.db.save_session(&fork).await?;
        Ok(fork)
    }
    
    pub async fn merge(&self, base: &Session, branch: &Session) -> Result<Session> {
        // Three-way merge of messages
        // Resolve conflicts
        // Create merged session
    }
    
    pub async fn search(&self, query: &str) -> Result<Vec<Session>> {
        // FTS5 search across sessions
        self.db.search_sessions(query).await
    }
    
    pub async fn get_ancestors(&self, session: &Session) -> Vec<Session> {
        // Traverse DAG upward
        // Get all parent sessions
    }
}
```

**Acceptance Criteria:**
- [ ] Sessions save/load
- [ ] Fork/merge works
- [ ] Search works

---

## Epic 3 Complete!

**Success Criteria:**
- [ ] Agent loop running
- [ ] Commander hierarchy
- [ ] 13+ LLM providers
- [ ] MCP client/server
- [ ] Session management

**Next:** Epic 4 - Complete Backend (Sprint 33)
