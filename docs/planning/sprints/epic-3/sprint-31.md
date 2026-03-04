# Sprint 31: Commander & LLM

**Epic:** Agent Core (Epic 3)  
**Duration:** 2 weeks  
**Goal:** Commander hierarchy, LLM providers, model router

## Stories

### Story 3.4: Commander (Praxis Hierarchy)
**Points:** 16 (AI: 8 hrs, Human: 8 hrs)

**What to build:**
Commander in `crates/ava-commander/src/lib.rs`

```rust
pub struct Commander {
    leads: Vec<Lead>,
    budget: Budget,
}

pub struct Lead {
    name: String,
    domain: Domain,
    workers: Vec<Worker>,
    model: Box<dyn LLMProvider>,
}

pub enum Domain {
    Frontend,
    Backend,
    QA,
    Research,
    Debug,
}

pub struct Worker {
    id: Uuid,
    lead: String,
    agent: AgentLoop,
    budget: Budget,
}

impl Commander {
    pub fn new(budget: Budget) -> Self {
        Self {
            leads: vec![
                Lead::new("Frontend", Domain::Frontend),
                Lead::new("Backend", Domain::Backend),
                Lead::new("QA", Domain::QA),
                Lead::new("Research", Domain::Research),
                Lead::new("Debug", Domain::Debug),
            ],
            budget,
        }
    }
    
    pub async fn delegate(&self, task: Task) -> Result<Worker> {
        // Analyze task to determine domain
        let domain = self.analyze_task(&task);
        
        // Find appropriate lead
        let lead = self.leads.iter()
            .find(|l| l.domain == domain)
            .ok_or(Error::NoLeadForDomain)?;
        
        // Spawn worker
        let worker = lead.spawn_worker(task, &self.budget).await?;
        
        Ok(worker)
    }
    
    pub async fn coordinate(&self, workers: &[Worker]) -> Result<Session> {
        // Coordinate multiple workers
        // Collect results
        // Merge outputs
    }
    
    fn analyze_task(&self, task: &Task) -> Domain {
        // ML-based or heuristic task classification
    }
}

impl Lead {
    pub async fn spawn_worker(&self, task: Task, budget: &Budget) -> Result<Worker> {
        let worker_budget = budget.allocate();
        let agent = AgentLoop::new(self.model.clone(), worker_budget);
        
        Ok(Worker {
            id: Uuid::new_v4(),
            lead: self.name.clone(),
            agent,
            budget: worker_budget,
        })
    }
}
```

**Acceptance Criteria:**
- [ ] 5 leads created
- [ ] Task routing works
- [ ] Worker spawning works

---

### Story 3.5: LLM Provider Abstraction
**Points:** 16 (AI: 8 hrs, Human: 8 hrs)

**What to build:**
LLM providers in `crates/ava-llm/src/lib.rs`

```rust
#[async_trait]
pub trait LLMProvider: Send + Sync {
    async fn generate(&self, messages: &[Message]) -> Result<String>;
    async fn generate_stream(&self, messages: &[Message]) -> Result<Box<dyn Stream<Item = String>>>;
    fn estimate_tokens(&self, text: &str) -> usize;
    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64;
    fn model_name(&self) -> &str;
}

pub struct OpenAIProvider {
    client: reqwest::Client,
    api_key: String,
    model: String,
}

#[async_trait]
impl LLMProvider for OpenAIProvider {
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let response = self.client
            .post("https://api.openai.com/v1/chat/completions")
            .json(&json!({
                "model": self.model,
                "messages": messages,
            }))
            .send()
            .await?;
            
        let json: Value = response.json().await?;
        Ok(json["choices"][0]["message"]["content"].as_str().unwrap().to_string())
    }
    
    async fn generate_stream(&self, messages: &[Message]) -> Result<Box<dyn Stream<Item = String>>> {
        // SSE streaming
    }
    
    fn estimate_tokens(&self, text: &str) -> usize {
        // Tiktoken or approximation
        text.len() / 4
    }
    
    fn estimate_cost(&self, input: usize, output: usize) -> f64 {
        let input_cost = input as f64 * 0.00001;
        let output_cost = output as f64 * 0.00003;
        input_cost + output_cost
    }
    
    fn model_name(&self) -> &str {
        &self.model
    }
}

// Similar implementations for:
pub struct AnthropicProvider;
pub struct OpenRouterProvider;
pub struct OllamaProvider;
pub struct GeminiProvider;
// ... 9 more providers
```

**Acceptance Criteria:**
- [ ] 13+ providers implemented
- [ ] Streaming works
- [ ] Cost tracking works

---

### Story 3.6: Model Router
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to build:**
Model router in `crates/ava-llm/src/router.rs`

```rust
pub struct ModelRouter {
    providers: HashMap<String, Box<dyn LLMProvider>>,
    default: String,
}

impl ModelRouter {
    pub fn new() -> Self {
        let mut providers: HashMap<String, Box<dyn LLMProvider>> = HashMap::new();
        providers.insert("gpt-4".to_string(), Box::new(OpenAIProvider::new("gpt-4")));
        providers.insert("claude-3".to_string(), Box::new(AnthropicProvider::new("claude-3-opus")));
        // ... more providers
        
        Self {
            providers,
            default: "gpt-4".to_string(),
        }
    }
    
    pub fn route(&self, task: &Task) -> &dyn LLMProvider {
        // Route based on:
        // - Task type (planning → strong model, simple → cheap model)
        // - Cost constraints
        // - Speed requirements
        // - Context size
        
        match task.task_type {
            TaskType::Planning => self.providers.get("claude-3").unwrap().as_ref(),
            TaskType::CodeGeneration => self.providers.get("gpt-4").unwrap().as_ref(),
            TaskType::Simple => self.providers.get("gpt-3.5").unwrap().as_ref(),
            TaskType::LargeContext => self.providers.get("gemini-pro").unwrap().as_ref(),
            _ => self.providers.get(&self.default).unwrap().as_ref(),
        }
    }
    
    pub fn get(&self, name: &str) -> Option<&dyn LLMProvider> {
        self.providers.get(name).map(|p| p.as_ref())
    }
}
```

**Acceptance Criteria:**
- [ ] Routing logic works
- [ ] Per-task model selection
- [ ] Cost optimization

---

## Sprint Goal

**Success Criteria:**
- [ ] Commander hierarchy working
- [ ] 13+ LLM providers
- [ ] Smart routing

**Next:** Sprint 32 - MCP & Integration
