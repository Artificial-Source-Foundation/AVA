# Sprint 33: Remaining Tools

**Epic:** Complete Backend (Epic 4)  
**Duration:** 2 weeks  
**Goal:** Git tools, browser, memory, permissions

## Stories

### Story 4.1: Git Tools
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
Git tools in `crates/ava-tools/src/git/mod.rs`

```rust
pub struct GitTool;

pub enum GitAction {
    Commit { message: String },
    Branch { name: String },
    Checkout { branch: String },
    PR { title: String, body: String },
    Diff,
    Log { limit: usize },
    Status,
}

#[async_trait]
impl Tool for GitTool {
    fn name(&self) -> &str { "git" }
    
    async fn execute(&self, args: Value) -> Result<ToolResult> {
        let action: GitAction = serde_json::from_value(args)?;
        
        match action {
            GitAction::Commit { message } => self.commit(&message).await,
            GitAction::Branch { name } => self.branch(&name).await,
            GitAction::Checkout { branch } => self.checkout(&branch).await,
            GitAction::PR { title, body } => self.create_pr(&title, &body).await,
            GitAction::Diff => self.diff().await,
            GitAction::Log { limit } => self.log(limit).await,
            GitAction::Status => self.status().await,
        }
    }
}

impl GitTool {
    async fn commit(&self, message: &str) -> Result<ToolResult> {
        let output = Command::new("git")
            .args(&["commit", "-m", message])
            .output()
            .await?;
            
        Ok(ToolResult {
            content: String::from_utf8_lossy(&output.stdout).to_string(),
            is_error: !output.status.success(),
        })
    }
    
    async fn create_pr(&self, title: &str, body: &str) -> Result<ToolResult> {
        // Use gh CLI or GitHub API
        let output = Command::new("gh")
            .args(&["pr", "create", "--title", title, "--body", body])
            .output()
            .await?;
            
        Ok(ToolResult {
            content: String::from_utf8_lossy(&output.stdout).to_string(),
            is_error: !output.status.success(),
        })
    }
    
    // ... other git operations
}
```

**Acceptance Criteria:**
- [ ] 6 git operations work
- [ ] PR creation works
- [ ] Clean integration

---

### Story 4.2: Browser Tool
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
Browser tool in `crates/ava-tools/src/browser.rs`

```rust
pub struct BrowserTool {
    driver: WebDriver,
}

impl BrowserTool {
    pub async fn new() -> Result<Self> {
        let driver = WebDriver::new("http://localhost:9515").await?;
        Ok(Self { driver })
    }
    
    async fn navigate(&self, url: &str) -> Result<ToolResult> {
        self.driver.goto(url).await?;
        Ok(ToolResult::success("Navigated"))
    }
    
    async fn click(&self, selector: &str) -> Result<ToolResult> {
        let element = self.driver.find(By::Css(selector)).await?;
        element.click().await?;
        Ok(ToolResult::success("Clicked"))
    }
    
    async fn type_text(&self, selector: &str, text: &str) -> Result<ToolResult> {
        let element = self.driver.find(By::Css(selector)).await?;
        element.send_keys(text).await?;
        Ok(ToolResult::success("Typed"))
    }
    
    async fn extract(&self) -> Result<ToolResult> {
        // Get page content
        // Convert to accessibility tree
        let body = self.driver.find(By::Css("body")).await?;
        let text = body.text().await?;
        Ok(ToolResult::success(&text))
    }
    
    async fn screenshot(&self) -> Result<ToolResult> {
        let screenshot = self.driver.screenshot().await?;
        // Save and return path
        Ok(ToolResult::success("screenshot.png"))
    }
}

#[async_trait]
impl Tool for BrowserTool {
    fn name(&self) -> &str { "browser" }
    
    async fn execute(&self, args: Value) -> Result<ToolResult> {
        let action = args["action"].as_str().unwrap();
        
        match action {
            "navigate" => self.navigate(args["url"].as_str().unwrap()).await,
            "click" => self.click(args["selector"].as_str().unwrap()).await,
            "type" => self.type_text(
                args["selector"].as_str().unwrap(),
                args["text"].as_str().unwrap()
            ).await,
            "extract" => self.extract().await,
            "screenshot" => self.screenshot().await,
            _ => Err(Error::UnknownAction),
        }
    }
}
```

**Acceptance Criteria:**
- [ ] Browser automation works
- [ ] Can navigate, click, extract
- [ ] Accessibility tree support

---

### Story 4.3: Memory System
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to build:**
Memory in `crates/ava-memory/src/lib.rs`

```rust
pub struct MemorySystem {
    db: Database,
}

impl MemorySystem {
    pub async fn remember(&self, key: &str, value: &str) -> Result<()> {
        let memory = Memory {
            id: Uuid::new_v4(),
            key: key.to_string(),
            value: value.to_string(),
            created_at: Utc::now(),
        };
        
        self.db.save_memory(&memory).await
    }
    
    pub async fn recall(&self, key: &str) -> Result<Option<String>> {
        let memory = self.db.load_memory(key).await?;
        Ok(memory.map(|m| m.value))
    }
    
    pub async fn search(&self, query: &str) -> Result<Vec<Memory>> {
        // FTS5 search
        self.db.search_memories(query).await
    }
    
    pub async fn get_recent(&self, limit: usize) -> Result<Vec<Memory>> {
        self.db.get_recent_memories(limit).await
    }
}

pub struct Memory {
    pub id: Uuid,
    pub key: String,
    pub value: String,
    pub created_at: DateTime<Utc>,
}
```

**Acceptance Criteria:**
- [ ] Remember/recall works
- [ ] FTS5 search works
- [ ] Cross-session persistence

---

### Story 4.4: Permission System
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
Permissions in `crates/ava-permissions/src/lib.rs`

```rust
pub struct PermissionSystem {
    rules: Vec<Rule>,
}

pub struct Rule {
    pub tool: String,
    pub pattern: Pattern,
    pub action: Action,
}

pub enum Pattern {
    Any,
    Glob(String),  // e.g., "*.rs"
    Regex(String),
    Path(PathBuf),
}

pub enum Action {
    Allow,
    Deny,
    Ask,
}

impl PermissionSystem {
    pub fn load(config: &Config) -> Result<Self> {
        let rules = config.permissions.iter()
            .map(|r| Rule::parse(r))
            .collect::<Result<Vec<_>>>()?;
            
        Ok(Self { rules })
    }
    
    pub fn evaluate(&self, tool: &str, args: &Value) -> Permission {
        // Check rules in order
        for rule in &self.rules {
            if rule.tool == tool && rule.matches(args) {
                return match rule.action {
                    Action::Allow => Permission::Allowed,
                    Action::Deny => Permission::Denied,
                    Action::Ask => Permission::RequiresApproval,
                };
            }
        }
        
        // Default: ask
        Permission::RequiresApproval
    }
    
    pub fn dynamic_check(&self, tool: &str, args: &Value) -> bool {
        // Dynamic escalation based on:
        // - Files outside workspace
        // - Destructive operations
        // - Network requests
        // etc.
        
        if self.is_outside_workspace(args) {
            return false; // Requires approval
        }
        
        if self.is_destructive(tool, args) {
            return false;
        }
        
        true
    }
}
```

**Acceptance Criteria:**
- [ ] Four-tier rules work
- [ ] Dynamic escalation works
- [ ] Permission denied blocks

---

## Sprint Goal

**Success Criteria:**
- [ ] Git tools working
- [ ] Browser automation
- [ ] Memory system
- [ ] Permission system

**Next:** Sprint 34 - Extensions & Validation
