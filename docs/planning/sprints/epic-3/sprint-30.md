# Sprint 30: Agent Loop

**Epic:** Agent Core (Epic 3)  
**Duration:** 2 weeks  
**Goal:** Async agent loop, tool registry, context manager

## Stories

### Story 3.1: Async Agent Loop
**Points:** 16 (AI: 8 hrs, Human: 8 hrs)

**What to build:**
Agent loop in `crates/ava-agent/src/loop.rs`

```rust
pub struct AgentLoop {
    llm: Box<dyn LLMProvider>,
    tools: ToolRegistry,
    context: ContextManager,
    config: AgentConfig,
}

impl AgentLoop {
    pub async fn run(&mut self, goal: &str) -> Result<Session> {
        let mut session = Session::new();
        
        loop {
            // Generate LLM response
            let response = self.llm.generate(&self.context.get_messages()).await?;
            
            // Parse tool calls
            let tool_calls = parse_tool_calls(&response);
            
            // Execute tools
            for call in tool_calls {
                let result = self.tools.execute(call).await?;
                self.context.add_tool_result(&result);
                
                // Check for completion
                if call.name == "attempt_completion" {
                    return Ok(session);
                }
            }
            
            // Compact context if needed
            if self.context.should_compact() {
                self.context.compact().await?;
            }
        }
    }
    
    pub async fn run_streaming(&mut self, goal: &str) -> impl Stream<Item = AgentEvent> {
        // Stream: tokens, tool calls, results, progress
    }
}

pub enum AgentEvent {
    Token(String),
    ToolCall(ToolCall),
    ToolResult(ToolResult),
    Progress(String),
    Complete(Session),
}
```

**Acceptance Criteria:**
- [ ] Agent loop runs
- [ ] Tool calls parsed
- [ ] Tools executed
- [ ] Streaming works

---

### Story 3.2: Tool Registry
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
Tool registry in `crates/ava-tools/src/registry.rs`

```rust
pub struct ToolRegistry {
    tools: HashMap<String, Box<dyn Tool>>,
    middleware: Vec<Box<dyn Middleware>>,
}

#[async_trait]
pub trait Tool: Send + Sync {
    fn name(&self) -> &str;
    fn description(&self) -> &str;
    fn parameters(&self) -> &Value;
    async fn execute(&self, args: Value) -> Result<ToolResult>;
}

#[async_trait]
pub trait Middleware: Send + Sync {
    async fn before(&self, call: &ToolCall) -> Result<()>;
    async fn after(&self, call: &ToolCall, result: &ToolResult) -> Result<ToolResult>;
}

impl ToolRegistry {
    pub fn new() -> Self {
        Self {
            tools: HashMap::new(),
            middleware: Vec::new(),
        }
    }
    
    pub fn register(&mut self, tool: Box<dyn Tool>) {
        self.tools.insert(tool.name().to_string(), tool);
    }
    
    pub fn add_middleware(&mut self, middleware: Box<dyn Middleware>) {
        self.middleware.push(middleware);
    }
    
    pub async fn execute(&self, call: ToolCall) -> Result<ToolResult> {
        // Run middleware before
        for mw in &self.middleware {
            mw.before(&call).await?;
        }
        
        // Execute tool
        let tool = self.tools.get(&call.name)
            .ok_or_else(|| Error::ToolNotFound(call.name.clone()))?;
        let result = tool.execute(call.arguments).await?;
        
        // Run middleware after
        let mut result = result;
        for mw in &self.middleware {
            result = mw.after(&call, &result).await?;
        }
        
        Ok(result)
    }
    
    pub fn list_tools(&self) -> Vec<&dyn Tool> {
        self.tools.values().map(|t| t.as_ref()).collect()
    }
}
```

**Acceptance Criteria:**
- [ ] 35 tools registered
- [ ] Middleware works
- [ ] Execution works

---

### Story 3.3: Context Manager
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
Context manager in `crates/ava-context/src/manager.rs`

```rust
pub struct ContextManager {
    messages: Vec<Message>,
    token_counter: TokenCounter,
    token_limit: usize,
    condensers: CondenserSelector,
}

impl ContextManager {
    pub fn new(limit: usize) -> Self {
        Self {
            messages: Vec::new(),
            token_counter: TokenCounter::new(),
            token_limit: limit,
            condensers: CondenserSelector::new(),
        }
    }
    
    pub fn add_message(&mut self, msg: Message) {
        self.messages.push(msg);
    }
    
    pub fn add_tool_result(&mut self, result: &ToolResult) {
        let msg = Message {
            role: Role::Tool,
            content: result.content.clone(),
            ..Default::default()
        };
        self.add_message(msg);
    }
    
    pub fn get_messages(&self) -> &[Message] {
        &self.messages
    }
    
    pub fn token_count(&self) -> usize {
        self.token_counter.count(&self.messages)
    }
    
    pub fn should_compact(&self) -> bool {
        self.token_count() > self.token_limit * 0.8
    }
    
    pub async fn compact(&mut self) -> Result<()> {
        let condenser = self.condensers.select(self.token_count(), self.token_limit);
        self.messages = condenser.condense(&self.messages)?;
        Ok(())
    }
    
    pub fn get_system_message(&self) -> String {
        // Build system prompt with tool descriptions
    }
}
```

**Acceptance Criteria:**
- [ ] Token counting works
- [ ] Compaction triggers
- [ ] Context stays under limit

---

## Sprint Goal

**Success Criteria:**
- [ ] Agent loop running
- [ ] 35 tools in registry
- [ ] Context management working

**Next:** Sprint 31 - Commander & LLM
