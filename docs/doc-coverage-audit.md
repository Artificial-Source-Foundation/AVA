# Documentation Coverage Audit

## Summary
- **Total public items**: 482
- **Documented**: 189 (39.2%)
- **Undocumented**: 293 (60.8%)

### Crate-Level `//!` Docs
- Crates with `//!` docs in lib.rs: 21/22
- **Missing crate-level docs**: ava-validator

---

## Per-Crate Breakdown

### ✅ `ava-agent`
- Public items: 16
- Documented: 16 / 16 (100.0%)

### ✅ `ava-auth`
- Public items: 24
- Documented: 24 / 24 (100.0%)

### ✅ `ava-cli-providers`
- Public items: 15
- Documented: 12 / 15 (80.0%)
- **Undocumented items** (3):
  - `pub struct CLIAgentRunner` at `ava-cli-providers/src/runner/mod.rs:10`
  - `pub struct RunOptions` at `ava-cli-providers/src/runner/mod.rs:16`
  - `pub struct TokenUsage` at `ava-cli-providers/src/config.rs:58`

### ❌ `ava-codebase`
- Public items: 18
- Documented: 2 / 18 (11.1%)
- **Undocumented items** (16):
  - `pub struct RepoFile` at `ava-codebase/src/repomap.rs:7`
  - `pub struct RankedFile` at `ava-codebase/src/repomap.rs:14`
  - `pub fn generate_repomap` at `ava-codebase/src/repomap.rs:19`
  - `pub fn select_relevant_files` at `ava-codebase/src/repomap.rs:43`
  - `pub fn score_map` at `ava-codebase/src/repomap.rs:47`
  - `pub fn calculate_pagerank` at `ava-codebase/src/pagerank.rs:5`
  - `pub fn extract_keywords` at `ava-codebase/src/pagerank.rs:42`
  - `pub fn calculate_relevance` at `ava-codebase/src/pagerank.rs:51`
  - `pub type Result` at `ava-codebase/src/error.rs:3`
  - `pub enum CodebaseError` at `ava-codebase/src/error.rs:6`
  - `pub struct SearchIndex` at `ava-codebase/src/search.rs:11`
  - `pub struct SearchDocument` at `ava-codebase/src/types.rs:2`
  - `pub struct SearchHit` at `ava-codebase/src/types.rs:24`
  - `pub struct SearchQuery` at `ava-codebase/src/types.rs:31`
  - `pub fn healthcheck` at `ava-codebase/src/lib.rs:27`
  - `pub struct DependencyGraph` at `ava-codebase/src/graph.rs:6`

### ❌ `ava-praxis`
- Public items: 32
- Documented: 8 / 32 (25.0%)
- **Undocumented items** (24):
  - `pub struct ReviewResult` at `ava-praxis/src/review.rs:17`
  - `pub struct ReviewIssue` at `ava-praxis/src/review.rs:26`
  - `pub enum Severity` at `ava-praxis/src/review.rs:34`
  - `pub enum ReviewVerdict` at `ava-praxis/src/review.rs:68`
  - `pub struct ReviewContext` at `ava-praxis/src/review.rs:85`
  - `pub struct DiffStats` at `ava-praxis/src/review.rs:91`
  - `pub enum DiffMode` at `ava-praxis/src/review.rs:98`
  - `pub async fn collect_diff` at `ava-praxis/src/review.rs:130`
  - `pub fn build_review_system_prompt` at `ava-praxis/src/review.rs:187`
  - `pub fn parse_review_output` at `ava-praxis/src/review.rs:233`
  - `pub fn format_text` at `ava-praxis/src/review.rs:327`
  - `pub fn format_json` at `ava-praxis/src/review.rs:374`
  - `pub fn format_markdown` at `ava-praxis/src/review.rs:378`
  - `pub fn determine_exit_code` at `ava-praxis/src/review.rs:425`
  - `pub async fn run_review_agent` at `ava-praxis/src/review.rs:440`
  - `pub enum PraxisEvent` at `ava-praxis/src/events.rs:5`
  - `pub struct Director` at `ava-praxis/src/lib.rs:38`
  - `pub struct DirectorConfig` at `ava-praxis/src/lib.rs:43`
  - `pub struct Lead` at `ava-praxis/src/lib.rs:78`
  - `pub enum Domain` at `ava-praxis/src/lib.rs:87`
  - `pub struct Worker` at `ava-praxis/src/lib.rs:97`
  - `pub struct Budget` at `ava-praxis/src/lib.rs:107`
  - `pub struct Task` at `ava-praxis/src/lib.rs:114`
  - `pub enum TaskType` at `ava-praxis/src/lib.rs:121`

### ✅ `ava-config`
- Public items: 21
- Documented: 17 / 21 (81.0%)
- **Undocumented items** (4):
  - `pub fn known_providers` at `ava-config/src/credentials.rs:273`
  - `pub fn standard_env_var` at `ava-config/src/credentials.rs:294`
  - `pub fn provider_name` at `ava-config/src/credential_commands.rs:169`
  - `pub fn redact_key` at `ava-config/src/credential_commands.rs:198`

### ❌ `ava-context`
- Public items: 23
- Documented: 7 / 23 (30.4%)
- **Undocumented items** (16):
  - `pub struct ToolTruncationStrategy` at `ava-context/src/strategies/tool_truncation.rs:7`
  - `pub trait CondensationStrategy` at `ava-context/src/strategies/mod.rs:16`
  - `pub struct SlidingWindowStrategy` at `ava-context/src/strategies/sliding_window.rs:11`
  - `pub struct SummarizationStrategy` at `ava-context/src/strategies/summarization.rs:10`
  - `pub type Result` at `ava-context/src/error.rs:5`
  - `pub enum ContextError` at `ava-context/src/error.rs:8`
  - `pub struct ContextChunk` at `ava-context/src/types.rs:4`
  - `pub struct CondensationResult` at `ava-context/src/types.rs:10`
  - `pub struct CondenserConfig` at `ava-context/src/types.rs:17`
  - `pub struct ContextManager` at `ava-context/src/manager.rs:13`
  - `pub fn estimate_tokens_for_message` at `ava-context/src/token_tracker.rs:14`
  - `pub struct TokenTracker` at `ava-context/src/token_tracker.rs:34`
  - `pub fn healthcheck` at `ava-context/src/lib.rs:23`
  - `pub struct Condenser` at `ava-context/src/condenser.rs:13`
  - `pub fn create_condenser` at `ava-context/src/condenser.rs:157`
  - `pub fn create_full_condenser` at `ava-context/src/condenser.rs:173`

### ❌ `ava-db`
- Public items: 6
- Documented: 2 / 6 (33.3%)
- **Undocumented items** (4):
  - `pub struct MessageRecord` at `ava-db/src/models/message.rs:6`
  - `pub struct MessageRepository` at `ava-db/src/models/message.rs:16`
  - `pub struct SessionRecord` at `ava-db/src/models/session.rs:6`
  - `pub struct SessionRepository` at `ava-db/src/models/session.rs:15`

### ✅ `ava-extensions`
- Public items: 12
- Documented: 11 / 12 (91.7%)
- **Undocumented items** (1):
  - `pub struct WasmLoader` at `ava-extensions/src/wasm_loader.rs:5`

### ❌ `ava-llm`
- Public items: 45
- Documented: 16 / 45 (35.6%)
- **Undocumented items** (29):
  - `pub struct GeminiProvider` at `ava-llm/src/providers/gemini.rs:18`
  - `pub struct CircuitBreaker` at `ava-llm/src/circuit_breaker.rs:9`
  - `pub fn default_model_for_provider` at `ava-llm/src/credential_test.rs:12`
  - `pub async fn test_provider_credentials` at `ava-llm/src/credential_test.rs:23`
  - `pub trait LLMProvider` at `ava-llm/src/provider.rs:20`
  - `pub fn rate_limited_error` at `ava-llm/src/providers/common/mod.rs:13`
  - `pub fn reqwest_error` at `ava-llm/src/providers/common/mod.rs:25`
  - `pub async fn validate_status` at `ava-llm/src/providers/common/mod.rs:40`
  - `pub fn map_messages_openai` at `ava-llm/src/providers/common/message_mapping.rs:13`
  - `pub fn map_messages_anthropic` at `ava-llm/src/providers/common/message_mapping.rs:69`
  - `pub fn map_messages_gemini_parts` at `ava-llm/src/providers/common/message_mapping.rs:129`
  - `pub fn model_pricing_usd_per_million` at `ava-llm/src/providers/common/parsing.rs:5`
  - `pub fn estimate_cost_usd` at `ava-llm/src/providers/common/parsing.rs:62`
  - `pub fn estimate_tokens` at `ava-llm/src/providers/common/parsing.rs:66`
  - `pub fn parse_sse_lines` at `ava-llm/src/providers/common/parsing.rs:70`
  - `pub fn parse_openai_completion_payload` at `ava-llm/src/providers/common/parsing.rs:78`
  - `pub fn parse_openai_delta_payload` at `ava-llm/src/providers/common/parsing.rs:95`
  - `pub fn parse_anthropic_completion_payload` at `ava-llm/src/providers/common/parsing.rs:106`
  - `pub fn parse_anthropic_delta_payload` at `ava-llm/src/providers/common/parsing.rs:117`
  - `pub fn parse_ollama_completion_payload` at `ava-llm/src/providers/common/parsing.rs:129`
  - `pub fn parse_gemini_completion_payload` at `ava-llm/src/providers/common/parsing.rs:138`
  - `pub fn healthcheck` at `ava-llm/src/lib.rs:21`
  - `pub struct MockProvider` at `ava-llm/src/providers/mock.rs:14`
  - `pub struct ModelRouter` at `ava-llm/src/router.rs:19`
  - `pub struct OpenAIProvider` at `ava-llm/src/providers/openai.rs:27`
  - `pub struct OllamaProvider` at `ava-llm/src/providers/ollama.rs:16`
  - `pub struct PoolStats` at `ava-llm/src/pool.rs:18`
  - `pub struct AnthropicProvider` at `ava-llm/src/providers/anthropic.rs:27`
  - `pub struct OpenRouterProvider` at `ava-llm/src/providers/openrouter.rs:17`

### ✅ `ava-logger`
- Public items: 4
- Documented: 4 / 4 (100.0%)

### ❌ `ava-lsp`
- Public items: 7
- Documented: 0 / 7 (0.0%)
- **Undocumented items** (7):
  - `pub struct LspClient` at `ava-lsp/src/client.rs:13`
  - `pub type Result` at `ava-lsp/src/error.rs:3`
  - `pub enum LspError` at `ava-lsp/src/error.rs:6`
  - `pub fn encode_message` at `ava-lsp/src/transport.rs:7`
  - `pub fn decode_message` at `ava-lsp/src/transport.rs:11`
  - `pub async fn write_frame` at `ava-lsp/src/transport.rs:44`
  - `pub async fn read_frame` at `ava-lsp/src/transport.rs:51`

### ❌ `ava-mcp`
- Public items: 22
- Documented: 5 / 22 (22.7%)
- **Undocumented items** (17):
  - `pub struct ServerCapabilities` at `ava-mcp/src/client.rs:14`
  - `pub struct MCPTool` at `ava-mcp/src/client.rs:28`
  - `pub struct MCPClient` at `ava-mcp/src/client.rs:41`
  - `pub fn tool_call_from_request` at `ava-mcp/src/client.rs:205`
  - `pub struct JsonRpcMessage` at `ava-mcp/src/transport.rs:14`
  - `pub struct JsonRpcError` at `ava-mcp/src/transport.rs:29`
  - `pub trait MCPTransport` at `ava-mcp/src/transport.rs:69`
  - `pub fn encode_message` at `ava-mcp/src/transport.rs:79`
  - `pub fn decode_message` at `ava-mcp/src/transport.rs:83`
  - `pub struct StdioTransport` at `ava-mcp/src/transport.rs:170`
  - `pub struct HttpTransport` at `ava-mcp/src/transport.rs:239`
  - `pub struct MCPServerConfig` at `ava-mcp/src/config.rs:12`
  - `pub enum TransportType` at `ava-mcp/src/config.rs:25`
  - `pub struct MCPConfigFile` at `ava-mcp/src/config.rs:43`
  - `pub struct ExtensionManager` at `ava-mcp/src/manager.rs:17`
  - `pub fn healthcheck` at `ava-mcp/src/lib.rs:23`
  - `pub struct AVAMCPServer` at `ava-mcp/src/server.rs:10`

### ❌ `ava-memory`
- Public items: 2
- Documented: 0 / 2 (0.0%)
- **Undocumented items** (2):
  - `pub struct Memory` at `ava-memory/src/lib.rs:12`
  - `pub struct MemorySystem` at `ava-memory/src/lib.rs:20`

### ❌ `ava-permissions`
- Public items: 22
- Documented: 8 / 22 (36.4%)
- **Undocumented items** (14):
  - `pub trait PermissionInspector` at `ava-permissions/src/inspector.rs:31`
  - `pub struct PermissionPolicy` at `ava-permissions/src/policy.rs:6`
  - `pub enum SafetyTag` at `ava-permissions/src/tags.rs:6`
  - `pub enum RiskLevel` at `ava-permissions/src/tags.rs:18`
  - `pub struct ToolSafetyProfile` at `ava-permissions/src/tags.rs:27`
  - `pub enum AuditDecision` at `ava-permissions/src/audit.rs:6`
  - `pub struct AuditEntry` at `ava-permissions/src/audit.rs:15`
  - `pub struct AuditLog` at `ava-permissions/src/audit.rs:24`
  - `pub struct AuditSummary` at `ava-permissions/src/audit.rs:116`
  - `pub enum Action` at `ava-permissions/src/lib.rs:21`
  - `pub enum Pattern` at `ava-permissions/src/lib.rs:28`
  - `pub struct Rule` at `ava-permissions/src/lib.rs:36`
  - `pub struct PermissionSystem` at `ava-permissions/src/lib.rs:43`
  - `pub struct PathRisk` at `ava-permissions/src/path_safety.rs:6`

### ✅ `ava-platform`
- Public items: 9
- Documented: 9 / 9 (100.0%)

### ❌ `ava-sandbox`
- Public items: 15
- Documented: 0 / 15 (0.0%)
- **Undocumented items** (15):
  - `pub fn validate_policy` at `ava-sandbox/src/policy.rs:4`
  - `pub fn validate_request` at `ava-sandbox/src/policy.rs:13`
  - `pub trait SandboxBackend` at `ava-sandbox/src/lib.rs:14`
  - `pub struct LinuxSandbox` at `ava-sandbox/src/lib.rs:19`
  - `pub struct MacOsSandbox` at `ava-sandbox/src/lib.rs:20`
  - `pub fn select_backend` at `ava-sandbox/src/lib.rs:42`
  - `pub type Result` at `ava-sandbox/src/error.rs:3`
  - `pub enum SandboxError` at `ava-sandbox/src/error.rs:6`
  - `pub fn build_sandbox_exec_plan` at `ava-sandbox/src/macos.rs:5`
  - `pub struct SandboxPolicy` at `ava-sandbox/src/types.rs:2`
  - `pub struct SandboxRequest` at `ava-sandbox/src/types.rs:21`
  - `pub struct SandboxPlan` at `ava-sandbox/src/types.rs:29`
  - `pub fn build_bwrap_plan` at `ava-sandbox/src/linux.rs:5`
  - `pub struct SandboxOutput` at `ava-sandbox/src/executor.rs:10`
  - `pub async fn execute_plan` at `ava-sandbox/src/executor.rs:16`

### ❌ `ava-session`
- Public items: 8
- Documented: 0 / 8 (0.0%)
- **Undocumented items** (8):
  - `pub fn role_to_str` at `ava-session/src/helpers.rs:45`
  - `pub fn str_to_role` at `ava-session/src/helpers.rs:54`
  - `pub fn parse_uuid` at `ava-session/src/helpers.rs:64`
  - `pub fn parse_datetime` at `ava-session/src/helpers.rs:68`
  - `pub fn db_error` at `ava-session/src/helpers.rs:74`
  - `pub fn to_conversion_error` at `ava-session/src/helpers.rs:78`
  - `pub struct SessionManager` at `ava-session/src/lib.rs:21`
  - `pub fn healthcheck` at `ava-session/src/lib.rs:25`

### ❌ `ava-tools`
- Public items: 74
- Documented: 18 / 74 (24.3%)
- **Undocumented items** (56):
  - `pub trait Tool` at `ava-tools/src/registry.rs:17`
  - `pub struct PermissionMiddleware` at `ava-tools/src/permission_middleware.rs:10`
  - `pub fn healthcheck` at `ava-tools/src/lib.rs:17`
  - `pub struct EditRequest` at `ava-tools/src/edit/request.rs:2`
  - `pub struct EditResult` at `ava-tools/src/edit/mod.rs:18`
  - `pub struct EditEngine` at `ava-tools/src/edit/mod.rs:23`
  - `pub enum BrowserAction` at `ava-tools/src/browser.rs:5`
  - `pub struct BrowserResult` at `ava-tools/src/browser.rs:23`
  - `pub trait BrowserDriver` at `ava-tools/src/browser.rs:35`
  - `pub struct BrowserEngine` at `ava-tools/src/browser.rs:43`
  - `pub enum BrowserError` at `ava-tools/src/browser.rs:69`
  - `pub enum EditError` at `ava-tools/src/edit/error.rs:4`
  - `pub struct StreamingMatcher` at `ava-tools/src/edit/fuzzy_match.rs:8`
  - `pub struct StreamMatch` at `ava-tools/src/edit/fuzzy_match.rs:25`
  - `pub struct FuzzyMatchStrategy` at `ava-tools/src/edit/fuzzy_match.rs:34`
  - `pub struct WriteTool` at `ava-tools/src/core/write.rs:11`
  - `pub struct BashTool` at `ava-tools/src/core/bash.rs:16`
  - `pub struct SessionListTool` at `ava-tools/src/core/session_ops.rs:10`
  - `pub struct SessionLoadTool` at `ava-tools/src/core/session_ops.rs:75`
  - `pub enum GitAction` at `ava-tools/src/git/mod.rs:6`
  - `pub struct ToolResult` at `ava-tools/src/git/mod.rs:26`
  - `pub struct GitTool` at `ava-tools/src/git/mod.rs:35`
  - `pub enum GitToolError` at `ava-tools/src/git/mod.rs:107`
  - `pub struct TestRunnerTool` at `ava-tools/src/core/test_runner.rs:15`
  - `pub trait SelfCorrector` at `ava-tools/src/edit/recovery.rs:8`
  - `pub struct RecoveryResult` at `ava-tools/src/edit/recovery.rs:13`
  - `pub struct RecoveryPipeline` at `ava-tools/src/edit/recovery.rs:21`
  - `pub trait EditStrategy` at `ava-tools/src/edit/strategies/mod.rs:11`
  - `pub struct ExactMatchStrategy` at `ava-tools/src/edit/strategies/mod.rs:17`
  - `pub struct FlexibleMatchStrategy` at `ava-tools/src/edit/strategies/mod.rs:37`
  - `pub struct MultiEditTool` at `ava-tools/src/core/multiedit.rs:13`
  - `pub struct GrepTool` at `ava-tools/src/core/grep.rs:15`
  - `pub struct EditTool` at `ava-tools/src/core/edit.rs:12`
  - `pub struct SessionSearchTool` at `ava-tools/src/core/session_search.rs:10`
  - `pub struct CodebaseSearchTool` at `ava-tools/src/core/codebase_search.rs:11`
  - `pub struct ApplyPatchTool` at `ava-tools/src/core/apply_patch.rs:11`
  - `pub struct GlobTool` at `ava-tools/src/core/glob.rs:13`
  - `pub struct ParamDef` at `ava-tools/src/core/custom_tool.rs:22`
  - `pub enum ExecutionDef` at `ava-tools/src/core/custom_tool.rs:38`
  - `pub fn register_core_tools` at `ava-tools/src/core/mod.rs:29`
  - `pub fn register_memory_tools` at `ava-tools/src/core/mod.rs:43`
  - `pub fn register_codebase_tools` at `ava-tools/src/core/mod.rs:49`
  - `pub fn register_custom_tools` at `ava-tools/src/core/mod.rs:56`
  - `pub fn register_session_tools` at `ava-tools/src/core/mod.rs:60`
  - `pub struct RememberTool` at `ava-tools/src/core/memory.rs:10`
  - `pub struct RecallTool` at `ava-tools/src/core/memory.rs:64`
  - `pub struct MemorySearchTool` at `ava-tools/src/core/memory.rs:118`
  - `pub struct DiagnosticsTool` at `ava-tools/src/core/diagnostics.rs:11`
  - `pub struct BlockAnchorStrategy` at `ava-tools/src/edit/strategies/advanced.rs:8`
  - `pub struct RegexMatchStrategy` at `ava-tools/src/edit/strategies/advanced.rs:47`
  - `pub struct LineNumberStrategy` at `ava-tools/src/edit/strategies/advanced.rs:74`
  - `pub struct TokenBoundaryStrategy` at `ava-tools/src/edit/strategies/advanced.rs:107`
  - `pub struct IndentationAwareStrategy` at `ava-tools/src/edit/strategies/advanced.rs:131`
  - `pub struct MultiOccurrenceStrategy` at `ava-tools/src/edit/strategies/advanced.rs:168`
  - `pub struct LintTool` at `ava-tools/src/core/lint.rs:12`
  - `pub struct ReadTool` at `ava-tools/src/core/read.rs:14`

### ❌ `ava-tui`
- Public items: 87
- Documented: 20 / 87 (23.0%)
- **Undocumented items** (67):
  - `pub async fn run_auth` at `ava-tui/src/auth.rs:13`
  - `pub struct InputState` at `ava-tui/src/state/input.rs:4`
  - `pub enum MessageKind` at `ava-tui/src/state/messages.rs:7`
  - `pub struct UiMessage` at `ava-tui/src/state/messages.rs:18`
  - `pub struct MessageState` at `ava-tui/src/state/messages.rs:139`
  - `pub enum Action` at `ava-tui/src/state/keybinds.rs:6`
  - `pub struct KeyBinding` at `ava-tui/src/state/keybinds.rs:27`
  - `pub struct KeybindState` at `ava-tui/src/state/keybinds.rs:39`
  - `pub fn default_keybinds` at `ava-tui/src/state/keybinds.rs:79`
  - `pub struct SessionState` at `ava-tui/src/state/session.rs:7`
  - `pub fn load_theme` at `ava-tui/src/config/themes.rs:3`
  - `pub enum ToolApproval` at `ava-tui/src/state/permission.rs:7`
  - `pub struct ApprovalRequest` at `ava-tui/src/state/permission.rs:22`
  - `pub enum ApprovalStage` at `ava-tui/src/state/permission.rs:29`
  - `pub struct PermissionState` at `ava-tui/src/state/permission.rs:36`
  - `pub fn load_keybind_overrides` at `ava-tui/src/config/keybindings.rs:10`
  - `pub struct TokenUsage` at `ava-tui/src/state/agent.rs:12`
  - `pub enum AgentActivity` at `ava-tui/src/state/agent.rs:18`
  - `pub struct AgentState` at `ava-tui/src/state/agent.rs:35`
  - `pub async fn run_headless` at `ava-tui/src/headless.rs:15`
  - `pub struct CliArgs` at `ava-tui/src/config/cli.rs:6`
  - `pub enum Command` at `ava-tui/src/config/cli.rs:63`
  - `pub enum AuthCommand` at `ava-tui/src/config/cli.rs:74`
  - `pub struct ReviewArgs` at `ava-tui/src/config/cli.rs:95`
  - `pub enum ReviewFormat` at `ava-tui/src/config/cli.rs:138`
  - `pub enum FailOnSeverity` at `ava-tui/src/config/cli.rs:145`
  - `pub async fn run_review` at `ava-tui/src/review.rs:14`
  - `pub struct Theme` at `ava-tui/src/state/theme.rs:4`
  - `pub fn render_message` at `ava-tui/src/widgets/message.rs:5`
  - `pub enum AppEvent` at `ava-tui/src/event.rs:11`
  - `pub fn spawn_event_reader` at `ava-tui/src/event.rs:35`
  - `pub fn spawn_tick_timer` at `ava-tui/src/event.rs:59`
  - `pub fn tick_interval` at `ava-tui/src/event.rs:71`
  - `pub struct CommandItem` at `ava-tui/src/widgets/command_palette.rs:15`
  - `pub struct CommandPaletteState` at `ava-tui/src/widgets/command_palette.rs:23`
  - `pub fn highlight_code` at `ava-tui/src/rendering/syntax.rs:15`
  - `pub fn render_welcome` at `ava-tui/src/widgets/welcome.rs:15`
  - `pub struct SessionListState` at `ava-tui/src/widgets/session_list.rs:4`
  - `pub fn filter_sessions` at `ava-tui/src/widgets/session_list.rs:10`
  - `pub struct ModelOption` at `ava-tui/src/widgets/model_selector.rs:7`
  - `pub enum ModelSection` at `ava-tui/src/widgets/model_selector.rs:18`
  - `pub struct ModelSelectorState` at `ava-tui/src/widgets/model_selector.rs:41`
  - `pub fn render_diff` at `ava-tui/src/rendering/diff.rs:6`
  - `pub struct ToolListItem` at `ava-tui/src/widgets/tool_list.rs:11`
  - `pub struct ToolListState` at `ava-tui/src/widgets/tool_list.rs:18`
  - `pub fn render_tool_list` at `ava-tui/src/widgets/tool_list.rs:42`
  - `pub struct StreamingText` at `ava-tui/src/widgets/streaming_text.rs:2`
  - `pub enum AutocompleteTrigger` at `ava-tui/src/widgets/autocomplete.rs:2`
  - `pub struct AutocompleteItem` at `ava-tui/src/widgets/autocomplete.rs:8`
  - `pub struct AutocompleteState` at `ava-tui/src/widgets/autocomplete.rs:23`
  - `pub fn render_message_list` at `ava-tui/src/widgets/message_list.rs:8`
  - `pub struct MainLayout` at `ava-tui/src/ui/layout.rs:3`
  - `pub fn build_layout` at `ava-tui/src/ui/layout.rs:26`
  - `pub fn render_tool_approval_lines` at `ava-tui/src/widgets/tool_approval.rs:27`
  - `pub fn render_composer` at `ava-tui/src/widgets/composer.rs:9`
  - `pub struct AppState` at `ava-tui/src/app/mod.rs:38`
  - `pub enum ModalType` at `ava-tui/src/app/mod.rs:59`
  - `pub struct App` at `ava-tui/src/app/mod.rs:68`
  - `pub fn render_sidebar` at `ava-tui/src/ui/sidebar.rs:8`
  - `pub fn diff_preview_lines` at `ava-tui/src/widgets/diff_preview.rs:5`
  - `pub fn markdown_to_lines` at `ava-tui/src/rendering/markdown.rs:7`
  - `pub fn render` at `ava-tui/src/ui/mod.rs:15`
  - `pub struct StatusMessage` at `ava-tui/src/ui/status_bar.rs:15`
  - `pub enum StatusLevel` at `ava-tui/src/ui/status_bar.rs:22`
  - `pub fn render_top` at `ava-tui/src/ui/status_bar.rs:82`
  - `pub fn render_context_bar` at `ava-tui/src/ui/status_bar.rs:193`
  - `pub struct DialogState` at `ava-tui/src/widgets/dialog.rs:2`

### ❌ `ava-types`
- Public items: 12
- Documented: 2 / 12 (16.7%)
- **Undocumented items** (10):
  - `pub struct Message` at `ava-types/src/message.rs:10`
  - `pub enum Role` at `ava-types/src/message.rs:52`
  - `pub type Result` at `ava-types/src/error.rs:6`
  - `pub enum AvaError` at `ava-types/src/error.rs:9`
  - `pub struct Tool` at `ava-types/src/tool.rs:6`
  - `pub struct ToolCall` at `ava-types/src/tool.rs:13`
  - `pub struct ToolResult` at `ava-types/src/tool.rs:20`
  - `pub struct Session` at `ava-types/src/session.rs:10`
  - `pub struct Context` at `ava-types/src/context.rs:6`
  - `pub enum ThinkingLevel` at `ava-types/src/lib.rs:33`

### ✅ `ava-validator`
- Public items: 8
- Documented: 8 / 8 (100.0%)
- ⚠️ **Missing crate-level `//!` docs in lib.rs**

---

## Priority: Items That MUST Be Documented

These are public traits, structs, enums (especially error types) that lack doc comments.

### 🔴 Public Traits (CRITICAL - define API contracts)
- `pub trait PermissionInspector` at `ava-permissions/src/inspector.rs:31`
- `pub trait MCPTransport` at `ava-mcp/src/transport.rs:69`
- `pub trait CondensationStrategy` at `ava-context/src/strategies/mod.rs:16`
- `pub trait LLMProvider` at `ava-llm/src/provider.rs:20`
- `pub trait SandboxBackend` at `ava-sandbox/src/lib.rs:14`
- `pub trait Tool` at `ava-tools/src/registry.rs:17`
- `pub trait BrowserDriver` at `ava-tools/src/browser.rs:35`
- `pub trait SelfCorrector` at `ava-tools/src/edit/recovery.rs:8`
- `pub trait EditStrategy` at `ava-tools/src/edit/strategies/mod.rs:11`

### 🔴 Error/Result Types (CRITICAL - consumers need to handle these)
- `pub struct ReviewResult` at `ava-praxis/src/review.rs:17`
- `pub enum LspError` at `ava-lsp/src/error.rs:6`
- `pub enum AvaError` at `ava-types/src/error.rs:9`
- `pub struct ToolResult` at `ava-types/src/tool.rs:20`
- `pub struct JsonRpcError` at `ava-mcp/src/transport.rs:29`
- `pub enum ContextError` at `ava-context/src/error.rs:8`
- `pub struct CondensationResult` at `ava-context/src/types.rs:10`
- `pub enum CodebaseError` at `ava-codebase/src/error.rs:6`
- `pub enum SandboxError` at `ava-sandbox/src/error.rs:6`
- `pub struct EditResult` at `ava-tools/src/edit/mod.rs:18`
- `pub struct BrowserResult` at `ava-tools/src/browser.rs:23`
- `pub enum BrowserError` at `ava-tools/src/browser.rs:69`
- `pub enum EditError` at `ava-tools/src/edit/error.rs:4`
- `pub struct ToolResult` at `ava-tools/src/git/mod.rs:26`
- `pub enum GitToolError` at `ava-tools/src/git/mod.rs:107`
- `pub struct RecoveryResult` at `ava-tools/src/edit/recovery.rs:13`

### 🟡 Public Structs/Enums (IMPORTANT - part of public API)
- `pub struct LspClient` at `ava-lsp/src/client.rs:13`
- `pub struct ReviewIssue` at `ava-praxis/src/review.rs:26`
- `pub enum Severity` at `ava-praxis/src/review.rs:34`
- `pub enum ReviewVerdict` at `ava-praxis/src/review.rs:68`
- `pub struct ReviewContext` at `ava-praxis/src/review.rs:85`
- `pub struct DiffStats` at `ava-praxis/src/review.rs:91`
- `pub enum DiffMode` at `ava-praxis/src/review.rs:98`
- `pub enum PraxisEvent` at `ava-praxis/src/events.rs:5`
- `pub struct Director` at `ava-praxis/src/lib.rs:38`
- `pub struct DirectorConfig` at `ava-praxis/src/lib.rs:43`
- `pub struct Lead` at `ava-praxis/src/lib.rs:78`
- `pub enum Domain` at `ava-praxis/src/lib.rs:87`
- `pub struct Worker` at `ava-praxis/src/lib.rs:97`
- `pub struct Budget` at `ava-praxis/src/lib.rs:107`
- `pub struct Task` at `ava-praxis/src/lib.rs:114`
- `pub enum TaskType` at `ava-praxis/src/lib.rs:121`
- `pub struct Message` at `ava-types/src/message.rs:10`
- `pub enum Role` at `ava-types/src/message.rs:52`
- `pub struct WasmLoader` at `ava-extensions/src/wasm_loader.rs:5`
- `pub struct Tool` at `ava-types/src/tool.rs:6`
- `pub struct ToolCall` at `ava-types/src/tool.rs:13`
- `pub struct Session` at `ava-types/src/session.rs:10`
- `pub struct Context` at `ava-types/src/context.rs:6`
- `pub enum ThinkingLevel` at `ava-types/src/lib.rs:33`
- `pub struct PermissionPolicy` at `ava-permissions/src/policy.rs:6`
- `pub enum SafetyTag` at `ava-permissions/src/tags.rs:6`
- `pub enum RiskLevel` at `ava-permissions/src/tags.rs:18`
- `pub struct ToolSafetyProfile` at `ava-permissions/src/tags.rs:27`
- `pub struct ServerCapabilities` at `ava-mcp/src/client.rs:14`
- `pub struct MCPTool` at `ava-mcp/src/client.rs:28`
- `pub struct MCPClient` at `ava-mcp/src/client.rs:41`
- `pub enum AuditDecision` at `ava-permissions/src/audit.rs:6`
- `pub struct AuditEntry` at `ava-permissions/src/audit.rs:15`
- `pub struct AuditLog` at `ava-permissions/src/audit.rs:24`
- `pub struct AuditSummary` at `ava-permissions/src/audit.rs:116`
- `pub struct JsonRpcMessage` at `ava-mcp/src/transport.rs:14`
- `pub struct StdioTransport` at `ava-mcp/src/transport.rs:170`
- `pub struct HttpTransport` at `ava-mcp/src/transport.rs:239`
- `pub struct MCPServerConfig` at `ava-mcp/src/config.rs:12`
- `pub enum TransportType` at `ava-mcp/src/config.rs:25`
- `pub struct MCPConfigFile` at `ava-mcp/src/config.rs:43`
- `pub struct ExtensionManager` at `ava-mcp/src/manager.rs:17`
- `pub enum Action` at `ava-permissions/src/lib.rs:21`
- `pub enum Pattern` at `ava-permissions/src/lib.rs:28`
- `pub struct Rule` at `ava-permissions/src/lib.rs:36`
- `pub struct PermissionSystem` at `ava-permissions/src/lib.rs:43`
- `pub struct AVAMCPServer` at `ava-mcp/src/server.rs:10`
- `pub struct PathRisk` at `ava-permissions/src/path_safety.rs:6`
- `pub struct ToolTruncationStrategy` at `ava-context/src/strategies/tool_truncation.rs:7`
- `pub struct MessageRecord` at `ava-db/src/models/message.rs:6`
- `pub struct MessageRepository` at `ava-db/src/models/message.rs:16`
- `pub struct SessionRecord` at `ava-db/src/models/session.rs:6`
- `pub struct SessionRepository` at `ava-db/src/models/session.rs:15`
- `pub struct SlidingWindowStrategy` at `ava-context/src/strategies/sliding_window.rs:11`
- `pub struct SessionManager` at `ava-session/src/lib.rs:21`
- `pub struct SummarizationStrategy` at `ava-context/src/strategies/summarization.rs:10`
- `pub struct ContextChunk` at `ava-context/src/types.rs:4`
- `pub struct CondenserConfig` at `ava-context/src/types.rs:17`
- `pub struct ContextManager` at `ava-context/src/manager.rs:13`
- `pub struct RepoFile` at `ava-codebase/src/repomap.rs:7`
- `pub struct RankedFile` at `ava-codebase/src/repomap.rs:14`
- `pub struct TokenTracker` at `ava-context/src/token_tracker.rs:34`
- `pub struct SearchIndex` at `ava-codebase/src/search.rs:11`
- `pub struct SearchDocument` at `ava-codebase/src/types.rs:2`
- `pub struct SearchHit` at `ava-codebase/src/types.rs:24`
- `pub struct SearchQuery` at `ava-codebase/src/types.rs:31`
- `pub struct Condenser` at `ava-context/src/condenser.rs:13`
- `pub struct DependencyGraph` at `ava-codebase/src/graph.rs:6`
- `pub struct CLIAgentRunner` at `ava-cli-providers/src/runner/mod.rs:10`
- `pub struct RunOptions` at `ava-cli-providers/src/runner/mod.rs:16`
- `pub struct TokenUsage` at `ava-cli-providers/src/config.rs:58`
- `pub struct GeminiProvider` at `ava-llm/src/providers/gemini.rs:18`
- `pub struct CircuitBreaker` at `ava-llm/src/circuit_breaker.rs:9`
- `pub struct InputState` at `ava-tui/src/state/input.rs:4`
- `pub enum MessageKind` at `ava-tui/src/state/messages.rs:7`
- `pub struct UiMessage` at `ava-tui/src/state/messages.rs:18`
- `pub struct MessageState` at `ava-tui/src/state/messages.rs:139`
- `pub enum Action` at `ava-tui/src/state/keybinds.rs:6`
- `pub struct KeyBinding` at `ava-tui/src/state/keybinds.rs:27`
- `pub struct KeybindState` at `ava-tui/src/state/keybinds.rs:39`
- `pub struct SessionState` at `ava-tui/src/state/session.rs:7`
- `pub enum ToolApproval` at `ava-tui/src/state/permission.rs:7`
- `pub struct ApprovalRequest` at `ava-tui/src/state/permission.rs:22`
- `pub enum ApprovalStage` at `ava-tui/src/state/permission.rs:29`
- `pub struct PermissionState` at `ava-tui/src/state/permission.rs:36`
- `pub struct Memory` at `ava-memory/src/lib.rs:12`
- `pub struct MemorySystem` at `ava-memory/src/lib.rs:20`
- `pub struct MockProvider` at `ava-llm/src/providers/mock.rs:14`
- `pub struct ModelRouter` at `ava-llm/src/router.rs:19`
- `pub struct OpenAIProvider` at `ava-llm/src/providers/openai.rs:27`
- `pub struct TokenUsage` at `ava-tui/src/state/agent.rs:12`
- `pub enum AgentActivity` at `ava-tui/src/state/agent.rs:18`
- `pub struct AgentState` at `ava-tui/src/state/agent.rs:35`
- `pub struct CliArgs` at `ava-tui/src/config/cli.rs:6`
- `pub enum Command` at `ava-tui/src/config/cli.rs:63`
- `pub enum AuthCommand` at `ava-tui/src/config/cli.rs:74`
- `pub struct ReviewArgs` at `ava-tui/src/config/cli.rs:95`
- `pub enum ReviewFormat` at `ava-tui/src/config/cli.rs:138`
- `pub enum FailOnSeverity` at `ava-tui/src/config/cli.rs:145`
- `pub struct OllamaProvider` at `ava-llm/src/providers/ollama.rs:16`
- `pub struct PoolStats` at `ava-llm/src/pool.rs:18`
- `pub struct AnthropicProvider` at `ava-llm/src/providers/anthropic.rs:27`
- `pub struct Theme` at `ava-tui/src/state/theme.rs:4`
- `pub struct OpenRouterProvider` at `ava-llm/src/providers/openrouter.rs:17`
- `pub struct LinuxSandbox` at `ava-sandbox/src/lib.rs:19`
- `pub struct MacOsSandbox` at `ava-sandbox/src/lib.rs:20`
- `pub struct SandboxPolicy` at `ava-sandbox/src/types.rs:2`
- `pub struct SandboxRequest` at `ava-sandbox/src/types.rs:21`
- `pub struct SandboxPlan` at `ava-sandbox/src/types.rs:29`
- `pub enum AppEvent` at `ava-tui/src/event.rs:11`
- `pub struct CommandItem` at `ava-tui/src/widgets/command_palette.rs:15`
- `pub struct CommandPaletteState` at `ava-tui/src/widgets/command_palette.rs:23`
- `pub struct SandboxOutput` at `ava-sandbox/src/executor.rs:10`
- `pub struct SessionListState` at `ava-tui/src/widgets/session_list.rs:4`
- `pub struct ModelOption` at `ava-tui/src/widgets/model_selector.rs:7`
- `pub enum ModelSection` at `ava-tui/src/widgets/model_selector.rs:18`
- `pub struct ModelSelectorState` at `ava-tui/src/widgets/model_selector.rs:41`
- `pub struct ToolListItem` at `ava-tui/src/widgets/tool_list.rs:11`
- `pub struct ToolListState` at `ava-tui/src/widgets/tool_list.rs:18`
- `pub struct StreamingText` at `ava-tui/src/widgets/streaming_text.rs:2`
- `pub enum AutocompleteTrigger` at `ava-tui/src/widgets/autocomplete.rs:2`
- `pub struct AutocompleteItem` at `ava-tui/src/widgets/autocomplete.rs:8`
- `pub struct AutocompleteState` at `ava-tui/src/widgets/autocomplete.rs:23`
- `pub struct MainLayout` at `ava-tui/src/ui/layout.rs:3`
- `pub struct AppState` at `ava-tui/src/app/mod.rs:38`
- `pub enum ModalType` at `ava-tui/src/app/mod.rs:59`
- `pub struct App` at `ava-tui/src/app/mod.rs:68`
- `pub struct PermissionMiddleware` at `ava-tools/src/permission_middleware.rs:10`
- `pub struct StatusMessage` at `ava-tui/src/ui/status_bar.rs:15`
- `pub enum StatusLevel` at `ava-tui/src/ui/status_bar.rs:22`
- `pub struct DialogState` at `ava-tui/src/widgets/dialog.rs:2`
- `pub struct EditRequest` at `ava-tools/src/edit/request.rs:2`
- `pub struct EditEngine` at `ava-tools/src/edit/mod.rs:23`
- `pub enum BrowserAction` at `ava-tools/src/browser.rs:5`
- `pub struct BrowserEngine` at `ava-tools/src/browser.rs:43`
- `pub struct StreamingMatcher` at `ava-tools/src/edit/fuzzy_match.rs:8`
- `pub struct StreamMatch` at `ava-tools/src/edit/fuzzy_match.rs:25`
- `pub struct FuzzyMatchStrategy` at `ava-tools/src/edit/fuzzy_match.rs:34`
- `pub struct WriteTool` at `ava-tools/src/core/write.rs:11`
- `pub struct BashTool` at `ava-tools/src/core/bash.rs:16`
- `pub struct SessionListTool` at `ava-tools/src/core/session_ops.rs:10`
- `pub struct SessionLoadTool` at `ava-tools/src/core/session_ops.rs:75`
- `pub enum GitAction` at `ava-tools/src/git/mod.rs:6`
- `pub struct GitTool` at `ava-tools/src/git/mod.rs:35`
- `pub struct TestRunnerTool` at `ava-tools/src/core/test_runner.rs:15`
- `pub struct RecoveryPipeline` at `ava-tools/src/edit/recovery.rs:21`
- `pub struct ExactMatchStrategy` at `ava-tools/src/edit/strategies/mod.rs:17`
- `pub struct FlexibleMatchStrategy` at `ava-tools/src/edit/strategies/mod.rs:37`
- `pub struct MultiEditTool` at `ava-tools/src/core/multiedit.rs:13`
- `pub struct GrepTool` at `ava-tools/src/core/grep.rs:15`
- `pub struct EditTool` at `ava-tools/src/core/edit.rs:12`
- `pub struct SessionSearchTool` at `ava-tools/src/core/session_search.rs:10`
- `pub struct CodebaseSearchTool` at `ava-tools/src/core/codebase_search.rs:11`
- `pub struct ApplyPatchTool` at `ava-tools/src/core/apply_patch.rs:11`
- `pub struct GlobTool` at `ava-tools/src/core/glob.rs:13`
- `pub struct ParamDef` at `ava-tools/src/core/custom_tool.rs:22`
- `pub enum ExecutionDef` at `ava-tools/src/core/custom_tool.rs:38`
- `pub struct RememberTool` at `ava-tools/src/core/memory.rs:10`
- `pub struct RecallTool` at `ava-tools/src/core/memory.rs:64`
- `pub struct MemorySearchTool` at `ava-tools/src/core/memory.rs:118`
- `pub struct DiagnosticsTool` at `ava-tools/src/core/diagnostics.rs:11`
- `pub struct BlockAnchorStrategy` at `ava-tools/src/edit/strategies/advanced.rs:8`
- `pub struct RegexMatchStrategy` at `ava-tools/src/edit/strategies/advanced.rs:47`
- `pub struct LineNumberStrategy` at `ava-tools/src/edit/strategies/advanced.rs:74`
- `pub struct TokenBoundaryStrategy` at `ava-tools/src/edit/strategies/advanced.rs:107`
- `pub struct IndentationAwareStrategy` at `ava-tools/src/edit/strategies/advanced.rs:131`
- `pub struct MultiOccurrenceStrategy` at `ava-tools/src/edit/strategies/advanced.rs:168`
- `pub struct LintTool` at `ava-tools/src/core/lint.rs:12`
- `pub struct ReadTool` at `ava-tools/src/core/read.rs:14`

### 🟡 Functions With >3 Parameters (SHOULD document parameters)
- `pub fn estimate_cost_usd(...)` at `ava-llm/src/providers/common/parsing.rs:62`
- `pub fn render_tool_list(...)` at `ava-tui/src/widgets/tool_list.rs:42`

### 🟡 Functions Returning `Result` (SHOULD document error conditions)
- `pub async fn collect_diff(...)` at `ava-praxis/src/review.rs:130`
- `pub fn parse_review_output(...)` at `ava-praxis/src/review.rs:233`
- `pub fn format_text(...)` at `ava-praxis/src/review.rs:327`
- `pub fn format_json(...)` at `ava-praxis/src/review.rs:374`
- `pub fn format_markdown(...)` at `ava-praxis/src/review.rs:378`
- `pub fn determine_exit_code(...)` at `ava-praxis/src/review.rs:425`
- `pub fn decode_message(...)` at `ava-lsp/src/transport.rs:11`
- `pub async fn write_frame(...)` at `ava-lsp/src/transport.rs:44`
- `pub async fn read_frame(...)` at `ava-lsp/src/transport.rs:51`
- `pub fn tool_call_from_request(...)` at `ava-mcp/src/client.rs:205`
- `pub fn decode_message(...)` at `ava-mcp/src/transport.rs:83`
- `pub fn str_to_role(...)` at `ava-session/src/helpers.rs:54`
- `pub fn parse_uuid(...)` at `ava-session/src/helpers.rs:64`
- `pub fn parse_datetime(...)` at `ava-session/src/helpers.rs:68`
- `pub async fn run_auth(...)` at `ava-tui/src/auth.rs:13`
- `pub async fn validate_status(...)` at `ava-llm/src/providers/common/mod.rs:40`
- `pub fn parse_openai_completion_payload(...)` at `ava-llm/src/providers/common/parsing.rs:78`
- `pub fn parse_anthropic_completion_payload(...)` at `ava-llm/src/providers/common/parsing.rs:106`
- `pub fn parse_ollama_completion_payload(...)` at `ava-llm/src/providers/common/parsing.rs:129`
- `pub fn parse_gemini_completion_payload(...)` at `ava-llm/src/providers/common/parsing.rs:138`
- `pub fn load_keybind_overrides(...)` at `ava-tui/src/config/keybindings.rs:10`
- `pub async fn run_headless(...)` at `ava-tui/src/headless.rs:15`
- `pub async fn run_review(...)` at `ava-tui/src/review.rs:14`
- `pub fn validate_policy(...)` at `ava-sandbox/src/policy.rs:4`
- `pub fn validate_request(...)` at `ava-sandbox/src/policy.rs:13`
- `pub fn select_backend(...)` at `ava-sandbox/src/lib.rs:42`
- `pub fn build_bwrap_plan(...)` at `ava-sandbox/src/linux.rs:5`
- `pub async fn execute_plan(...)` at `ava-sandbox/src/executor.rs:16`

---

## AGENTS.md Compliance Notes

Per AGENTS.md, the project follows these standards:
- Max 300 lines per file
- All new CLI/agent features MUST be Rust
- Tool implementations need `Tool` trait docs

### Documentation Violations Summary

| Severity | Category | Count |
|----------|----------|-------|
| 🔴 CRITICAL | Undocumented public traits | 9 |
| 🔴 CRITICAL | Undocumented error types | 16 |
| 🟡 IMPORTANT | Undocumented structs/enums | 169 |
| 🟡 IMPORTANT | Undocumented fns with >3 params | 2 |
| 🟡 IMPORTANT | Undocumented fns returning Result | 28 |
| ⚠️ MINOR | Crates missing //! docs | 1 |
