<!-- Last verified: 2026-03-16. Run 'cargo test --workspace' to revalidate. -->

# AVA Crate Map

20 Rust crates under `crates/`. Total: 358 source files, ~104K LOC, 1,798 tests.

## Dependency Layers

```
Layer 0 (no ava deps):  ava-types, ava-memory, ava-sandbox, ava-extensions, ava-codebase, ava-auth, ava-permissions, ava-validator
Layer 1:                ava-db, ava-platform, ava-context, ava-config, ava-session (depend on ava-types)
Layer 2:                ava-llm (ava-types, ava-config, ava-auth)
                        ava-tools (ava-types, ava-config, ava-permissions, ava-platform, ava-sandbox, ava-codebase)
                        ava-mcp (ava-types, ava-tools)
Layer 3:                ava-cli-providers (ava-types, ava-llm)
                        ava-agent (ava-types, ava-llm, ava-tools, ava-config, ava-context, ava-permissions, ava-platform, ava-session, ava-memory, ava-mcp, ava-codebase, ava-praxis)
Layer 4:                ava-praxis (ava-types, ava-agent, ava-llm, ava-tools, ava-context, ava-platform, ava-cli-providers)
Layer 5 (top):          ava-tui (depends on nearly everything)
```

## Crate Details

### ava-types
- **Purpose**: Shared types used across all crates
- **Key types**: `Message`, `Role`, `Session`, `ToolCall`, `ToolResult`, `AvaError`, `Result`, `Context`, `TodoItem`, `ContextAttachment`, `ImageContent`
- **Depended on by**: Nearly every other crate
- **Stats**: 7 files, 1,755 LOC

### ava-agent
- **Purpose**: Core agent execution loop with tool calling, stuck detection, and mid-stream messaging
- **Key types**: `AgentLoop`, `AgentConfig`, `AgentEvent`, `AgentStack` (unified entrypoint), `MessageQueue` (3-tier steering/follow-up/post-complete), `ReflectionLoop`, `ErrorKind`
- **Key modules**: `agent_loop/` (tool execution, response parsing), `instructions.rs` (project instruction discovery), `stack.rs` (composed runtime), `system_prompt.rs`, `stuck.rs`, `message_queue.rs`
- **Depends on**: ava-types, ava-llm, ava-tools, ava-config, ava-context, ava-permissions, ava-platform, ava-session, ava-memory, ava-mcp, ava-codebase, ava-praxis
- **Stats**: 28 files, 10,251 LOC

### ava-llm
- **Purpose**: Unified LLM provider interface with routing, circuit breaking, and connection pooling
- **Key types**: `LLMProvider` (trait), `NormalizingProvider`, `ConnectionPool`, `ModelRouter`, `ProviderFactory` (trait), `CircuitBreaker`, `RetryBudget`, `ThinkingConfig`, `StreamChunk`, `FallbackChain`
- **Providers**: Anthropic, OpenAI-compatible, Gemini, Ollama, OpenRouter, Copilot, Inception, Mock
- **Depends on**: ava-types, ava-config, ava-auth
- **Stats**: 25 files, 11,658 LOC

### ava-tools
- **Purpose**: Tool trait, registry, tiered tool system, and TOML custom tool loader
- **Key types**: `Tool` (trait), `ToolRegistry`, `ToolSource` (BuiltIn/MCP/Custom), `ToolTier`, `CustomToolDef`, `ExecutionDef`
- **Default tools**: read, write, edit, bash, glob, grep
- **Extended tools**: apply_patch, web_fetch, web_search, multiedit, ast_ops, lsp_ops, code_search, git_read
- **Key modules**: `core/` (tool implementations), `edit/` (fuzzy match, recovery, strategies), `registry.rs`, `core/custom_tool.rs`
- **Depends on**: ava-types, ava-config, ava-permissions, ava-platform, ava-sandbox, ava-codebase
- **Stats**: 49 files, 10,641 LOC

### ava-permissions
- **Purpose**: Permission rules, bash command classification, risk levels, and path safety
- **Key types**: `DefaultInspector` (9-step evaluation), `CommandClassifier`, `SafetyTag`, `RiskLevel`, `PermissionPolicy` (permissive/standard/strict), `Action` (Allow/Deny/Ask)
- **Key modules**: `inspector.rs`, `classifier/`, `path_safety.rs`, `tags.rs`, `audit.rs`, `guardian.rs`, `injection.rs`
- **Depended on by**: ava-tools, ava-agent
- **Stats**: 19 files, 6,412 LOC

### ava-config
- **Purpose**: Configuration management, credential storage, model catalog, agent config
- **Key types**: `Config`, `Credentials`, `ModelCatalog`, `AgentsConfig`, `TrustStore`, `ThinkingBudgetConfig`, `RoutingConfig`
- **Key modules**: `credentials.rs`, `model_catalog/` (compiled-in registry.json), `agents.rs`, `trust.rs`, `routing.rs`, `thinking.rs`
- **Depends on**: ava-types, ava-auth
- **Stats**: 14 files, 5,071 LOC

### ava-praxis
- **Purpose**: Multi-agent orchestration with Director pattern, ACP, artifacts, peer communication
- **Key types**: `Director`, `PraxisEvent`, `Lead`, `Worker`, `AcpServer`, `AcpClient`, `ArtifactStore`, `Mailbox`, `SpecWorkflow`, `ConflictResolver`
- **Depends on**: ava-types, ava-agent, ava-llm, ava-tools, ava-context, ava-platform, ava-cli-providers
- **Stats**: 15 files, 3,691 LOC

### ava-context
- **Purpose**: Token tracking and context condensation (sliding window, summarization, tool truncation)
- **Key types**: `ContextManager`, `TokenTracker`, `Condenser`, `HybridCondenser`, `FocusChain`, `SlidingWindowStrategy`, `SummarizationStrategy`, `ToolTruncationStrategy`
- **Depends on**: ava-types
- **Stats**: 17 files, 3,079 LOC

### ava-mcp
- **Purpose**: Model Context Protocol client/server with stdio and HTTP transports
- **Key types**: `MCPClient`, `MCPTool`, `AVAMCPServer`, `MCPServerConfig`, `TransportType` (Stdio/Http), `MCPTransport` (trait), `StdioTransport`, `HttpTransport`, `InMemoryTransport`, `ExtensionManager`
- **Depends on**: ava-types, ava-tools
- **Stats**: 6 files, 1,786 LOC

### ava-session
- **Purpose**: Session persistence with SQLite, bookmarks, conversation tree, diff tracking
- **Key types**: `SessionStore` (trait), `Bookmark`, `TreeNode`, `DiffTracker`
- **Depends on**: ava-types
- **Stats**: 3 files, 1,609 LOC

### ava-cli-providers
- **Purpose**: External CLI agent integration and discovery (Claude Code, etc.)
- **Key types**: `CLIAgentConfig`, `CLIAgentLLMProvider`, `CLIAgentRunner`, `DiscoveredAgent`, `AgentRole`
- **Depends on**: ava-types, ava-llm
- **Stats**: 9 files, 1,510 LOC

### ava-auth
- **Purpose**: OAuth authentication (PKCE, device code), Copilot token exchange, API key management
- **Key types**: `AuthResult`, `AuthError`, `OAuthTokens`, `DeviceCodeResponse`, `AuthFlow`
- **No ava-* dependencies** (leaf crate)
- **Stats**: 8 files, 1,499 LOC

### ava-codebase
- **Purpose**: Code indexing with BM25 search, PageRank scoring, dependency graph, change impact analysis
- **Key types**: `CodebaseIndex`, `SearchIndex`, `DependencyGraph`, `SemanticIndex` (feature-gated), `ImpactSummary`, `RankedFile`
- **No ava-* dependencies** (leaf crate)
- **Stats**: 10 files, 1,269 LOC

### ava-platform
- **Purpose**: Platform abstractions for file system and shell operations
- **Key types**: `Platform` (trait), `StandardPlatform`, `FileSystem`, `Shell`, `CommandOutput`, `ExecuteOptions`
- **Depends on**: ava-types
- **Stats**: 4 files, 989 LOC

### ava-memory
- **Purpose**: Persistent key-value memory with SQLite and FTS5 full-text search
- **Key types**: `MemorySystem`, `Memory`, `LearnedMemory`, `LearnedMemoryStatus`
- **No ava-* dependencies** (leaf crate)
- **Stats**: 2 files, 778 LOC

### ava-sandbox
- **Purpose**: OS-level command sandboxing (bwrap on Linux, sandbox-exec on macOS)
- **Key types**: `SandboxBackend` (trait), `LinuxSandbox`, `MacOsSandbox`, `SandboxPlan`, `SandboxPolicy`, `SandboxRequest`
- **No ava-* dependencies** (leaf crate)
- **Stats**: 8 files, 670 LOC

### ava-extensions
- **Purpose**: Extension system with hook registration, native shared library, and WASM loaders
- **Key types**: `ExtensionManager`, `ExtensionDescriptor`, `HookRegistry`, `Hook`, `HookPoint`, `WasmLoader`
- **No ava-* dependencies** (leaf crate)
- **Stats**: 5 files, 509 LOC

### ava-db
- **Purpose**: SQLite connection pool and data models for sessions and messages
- **Key types**: `Database`, `SessionRepository`, `MessageRepository`, `SessionRecord`, `MessageRecord`
- **Depends on**: ava-types
- **Stats**: 4 files, 444 LOC

### ava-validator
- **Purpose**: Code validation pipeline with retry orchestration
- **Key types**: `ValidationPipeline`, `Validator` (trait), `FixGenerator` (trait), `CompilationValidator`, `SyntaxValidator`, `RetryOutcome`
- **No ava-* dependencies** (leaf crate)
- **Stats**: 3 files, 299 LOC

### ava-tui
- **Purpose**: Terminal user interface and headless CLI runner -- the primary AVA binary
- **Key modules**: `app/` (commands, event handling), `state/` (agent, message, permission, rewind, voice), `widgets/` (model selector, session list, tool list, token buffer), `headless/`, `rendering/`, `benchmark*` (feature-gated)
- **Binaries**: `ava` (main), `ava-smoke` (mock smoke test)
- **Depends on**: Nearly every other crate
- **Stats**: 95 files, 33,119 LOC
