# Sprint 29: LSP & Sandboxing

**Epic:** Essential Tools (Epic 2)  
**Duration:** 2 weeks  
**Goal:** LSP client, OS-level sandboxing, terminal security

## Stories

### Story 2.7: LSP Client
**Points:** 16 (AI: 8 hrs, Human: 8 hrs)

**What to build:**
LSP client in `crates/ava-lsp/src/client.rs`

```rust
pub struct LSPClient {
    connection: Connection,
    server_capabilities: ServerCapabilities,
}

impl LSPClient {
    pub async fn start(server_path: &Path) -> Result<Self> {
        // Spawn LSP server process
        // Initialize handshake
    }
    
    pub async fn goto_definition(&self, params: DefinitionParams) -> Result<Vec<Location>> {
        // Send textDocument/definition
        // Parse response
    }
    
    pub async fn get_diagnostics(&self, path: &Path) -> Result<Vec<Diagnostic>> {
        // Get errors/warnings
    }
    
    pub fn stream_diagnostics(&self) -> impl Stream<Item = Diagnostic> {
        // Real-time streaming
    }
    
    pub async fn shutdown(self) -> Result<()> {
        // Graceful shutdown
    }
}
```

**Acceptance Criteria:**
- [x] LSP transport and client foundation implemented
- [x] Diagnostics stream support implemented
- [x] Go-to-definition request/response flow implemented

---

### Story 2.8: OS-Level Sandboxing
**Points:** 16 (AI: 8 hrs, Human: 8 hrs)

**What to build:**
Sandboxing in `crates/ava-sandbox/src/lib.rs`

```rust
pub struct Sandbox {
    #[cfg(target_os = "linux")]
    landlock: LandlockRuleset,
    #[cfg(target_os = "macos")]
    seatbelt: SeatbeltProfile,
}

impl Sandbox {
    pub fn new() -> Result<Self> {
        #[cfg(target_os = "linux")]
        {
            // Landlock filesystem sandboxing
            let ruleset = LandlockRuleset::new()
                .add_rule(Path::new("/workspace"), AccessFs::ReadWrite)?
                .add_rule(Path::new("/tmp"), AccessFs::ReadWrite)?
                .restrict_self()?;
                
            // Seccomp BPF filtering
            let filter = SeccompFilter::new()
                .allow(Syscall::Open)
                .allow(Syscall::Read)
                .allow(Syscall::Write)
                .deny(Syscall::Execve)?;
                
            Ok(Self { landlock: ruleset, filter })
        }
        
        #[cfg(target_os = "macos")]
        {
            // Seatbelt profile
            let profile = SeatbeltProfile::minimal()
                .allow_path("/workspace");
                
            Ok(Self { seatbelt: profile })
        }
    }
    
    pub async fn execute(&self, command: &str) -> Result<Output> {
        // Execute in sandboxed environment
        // All network through proxy
    }
}
```

**Competitor Reference:** Codex CLI sandbox

**Acceptance Criteria:**
- [x] Sandbox backend abstraction implemented
- [x] Filesystem/network policy plans implemented
- [x] Platform-specific plan builders implemented

---

### Story 2.9: Terminal Security Classifier
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
Security classifier in `crates/ava-shell/src/security.rs`

```rust
pub struct SecurityClassifier;

#[derive(Debug, Clone, Copy)]
pub enum RiskLevel {
    Safe,
    Low,
    Medium,
    High,
    Critical,
}

impl SecurityClassifier {
    pub fn classify(&self, command: &str) -> RiskLevel {
        // Tree-sitter bash parsing
        let ast = parse_bash(command);
        
        // Check for dangerous patterns
        if self.has_pattern(&ast, Pattern::CurlPipeSh) {
            return RiskLevel::Critical;
        }
        if self.has_pattern(&ast, Pattern::RmRf) {
            return RiskLevel::High;
        }
        if self.has_pattern(&ast, Pattern::Sudo) {
            return RiskLevel::Medium;
        }
        
        RiskLevel::Safe
    }
    
    fn has_pattern(&self, ast: &AST, pattern: Pattern) -> bool {
        // Pattern matching on AST
    }
}
```

**Competitor Reference:** Continue's 1241-line classifier

**Acceptance Criteria:**
- [x] Dangerous commands flagged
- [x] Tree-sitter parsing works
- [x] Risk levels assigned

---

## Epic 2 Complete!

**Success Criteria:**
- [x] Edit tool: 90% success, 0.5s latency
- [x] BM25 search working
- [x] LSP client streaming foundation
- [x] Sandboxing foundation implemented

## Implementation Status (2026-03-04)

- Added `ava-lsp` crate with async frame transport, request builders, and diagnostics subscription
- Added `ava-sandbox` crate with Linux/macOS backend plan builders and policy validation
- Added AST-based terminal security classifier in `ava-permissions/src/classifier.rs`
- Verified with:
  - `cargo test -p ava-lsp -p ava-sandbox -p ava-permissions`
  - `cargo clippy -p ava-lsp -p ava-sandbox -p ava-permissions -- -D warnings`

**Next:** Epic 3 - Agent Core (Sprint 30)
