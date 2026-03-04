# Sprint 34: Extensions & Validation

**Epic:** Complete Backend (Epic 4)  
**Duration:** 2 weeks  
**Goal:** Extensions, validation, reflection loop

## Stories

### Story 4.5: Extension System
**Points:** 16 (AI: 8 hrs, Human: 8 hrs)

**What to build:**
Extensions in `crates/ava-extensions/src/lib.rs`

```rust
pub trait Extension: Send + Sync {
    fn name(&self) -> &str;
    fn version(&self) -> &str;
    fn register_tools(&self, registry: &mut ToolRegistry);
    fn register_hooks(&self, hooks: &mut HookRegistry);
    fn register_validators(&self, validators: &mut ValidatorRegistry);
}

pub struct ExtensionManager {
    extensions: Vec<Box<dyn Extension>>,
    wasm_runtime: Option<WasmRuntime>,
}

impl ExtensionManager {
    pub fn new() -> Self {
        Self {
            extensions: Vec::new(),
            wasm_runtime: None,
        }
    }
    
    pub fn load_native(&mut self, path: &Path) -> Result<()> {
        // Load dynamic library
        let lib = unsafe { Library::new(path)? };
        let create: Symbol<fn() -> Box<dyn Extension>> = unsafe {
            lib.get(b"create_extension")?
        };
        
        let extension = create();
        self.extensions.push(extension);
        
        // Keep library loaded (leak intentionally)
        std::mem::forget(lib);
        
        Ok(())
    }
    
    pub async fn load_wasm(&mut self, path: &Path) -> Result<()> {
        // WASM extension (sandboxed)
        let wasm_bytes = tokio::fs::read(path).await?;
        let module = wasmtime::Module::new(&self.wasm_engine, &wasm_bytes)?;
        
        // Create WASM extension wrapper
        let extension = WasmExtension::new(module)?;
        self.extensions.push(Box::new(extension));
        
        Ok(())
    }
    
    pub fn register_all(&self, registry: &mut ToolRegistry) {
        for ext in &self.extensions {
            ext.register_tools(registry);
        }
    }
    
    pub fn hot_reload(&mut self, name: &str) -> Result<()> {
        // Reload extension without restart
        // Development only
    }
}

pub struct HookRegistry {
    hooks: HashMap<HookPoint, Vec<Box<dyn Hook>>>,
}

pub enum HookPoint {
    BeforeToolCall,
    AfterToolCall,
    BeforeLLMCall,
    AfterLLMCall,
    OnSessionStart,
    OnSessionEnd,
}

#[async_trait]
pub trait Hook: Send + Sync {
    async fn invoke(&self, context: &mut HookContext) -> Result<()>;
}
```

**Acceptance Criteria:**
- [ ] Native extensions load
- [ ] WASM extensions work
- [ ] Hot reload works

---

### Story 4.6: Validation Pipeline
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
Validation in `crates/ava-validator/src/lib.rs`

```rust
pub struct ValidationPipeline {
    validators: Vec<Box<dyn Validator>>,
}

#[async_trait]
pub trait Validator: Send + Sync {
    async fn validate(&self, path: &Path, content: &str) -> Result<ValidationResult>;
}

pub struct SyntaxValidator;

#[async_trait]
impl Validator for SyntaxValidator {
    async fn validate(&self, path: &Path, content: &str) -> Result<ValidationResult> {
        // Tree-sitter syntax check
        let language = detect_language(path);
        let mut parser = tree_sitter::Parser::new();
        parser.set_language(language)?;
        
        let tree = parser.parse(content, None)
            .ok_or(Error::ParseError)?;
            
        if tree.root_node().has_error() {
            return Ok(ValidationResult::Invalid("Syntax error".to_string()));
        }
        
        Ok(ValidationResult::Valid)
    }
}

pub struct CompilationValidator;

#[async_trait]
impl Validator for CompilationValidator {
    async fn validate(&self, path: &Path, content: &str) -> Result<ValidationResult> {
        // Try to compile (if applicable)
        if is_rust_file(path) {
            // Run rustc --check
        } else if is_typescript_file(path) {
            // Run tsc --noEmit
        }
        
        Ok(ValidationResult::Valid)
    }
}

impl ValidationPipeline {
    pub async fn validate_edit(&self, path: &Path, content: &str) -> Result<()> {
        for validator in &self.validators {
            match validator.validate(path, content).await? {
                ValidationResult::Valid => continue,
                ValidationResult::Invalid(reason) => {
                    return Err(Error::ValidationFailed(reason));
                }
            }
        }
        
        Ok(())
    }
    
    pub async fn validate_with_retry(
        &self,
        path: &Path,
        edit: &Edit,
        agent: &mut AgentLoop
    ) -> Result<String> {
        // Try up to 3 times
        for attempt in 1..=3 {
            let content = apply_edit(edit)?;
            
            match self.validate_edit(path, &content).await {
                Ok(()) => return Ok(content),
                Err(e) if attempt < 3 => {
                    // Retry with error context
                    edit = agent.fix_edit(edit, &e.to_string()).await?;
                }
                Err(e) => return Err(e),
            }
        }
        
        unreachable!()
    }
}
```

**Competitor Reference:** Aider reflection loop

**Acceptance Criteria:**
- [ ] Syntax validation works
- [ ] Compilation check works
- [ ] Auto-retry works

---

### Story 4.7: Reflection Loop
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to build:**
Reflection in `crates/ava-agent/src/reflection.rs`

```rust
pub struct ReflectionLoop;

impl ReflectionLoop {
    pub async fn reflect_and_fix(
        &self,
        result: &ToolResult,
        agent: &mut AgentLoop
    ) -> Result<Option<ToolResult>> {
        if !result.is_error {
            return Ok(None);
        }
        
        // Check error type
        if let Some(fixable) = self.analyze_error(&result.content) {
            // Try to fix
            let fix = agent.generate_fix(&result.content).await?;
            let new_result = agent.execute_tool(&fix).await?;
            return Ok(Some(new_result));
        }
        
        Ok(None)
    }
    
    fn analyze_error(&self, error: &str) -> Option<ErrorType> {
        if error.contains("syntax error") {
            return Some(ErrorType::Syntax);
        }
        if error.contains("not found") {
            return Some(ErrorType::MissingImport);
        }
        if error.contains("type mismatch") {
            return Some(ErrorType::TypeError);
        }
        None
    }
}
```

**Acceptance Criteria:**
- [ ] Error detection works
- [ ] Auto-fix attempts work
- [ ] Better success rate

---

## Sprint Goal

**Success Criteria:**
- [ ] Extensions load
- [ ] Validation pipeline works
- [ ] Reflection improves results

**Next:** Sprint 35 - Performance & Polish
