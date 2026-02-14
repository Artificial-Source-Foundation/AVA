/**
 * @ava/core
 * Core business logic for Estela - LLM client, tools, types
 */

export type {
  A2AEvent,
  A2AMessage,
  A2AServerConfig,
  A2ATask,
  A2ATaskState,
  AgentAuthentication,
  AgentCapabilities,
  AgentCard,
  AgentProvider,
  AgentSkill,
  Artifact,
  AuthenticationScheme,
  AuthResult,
  CancelTaskResponse,
  DataPart,
  FilePart,
  GetTaskResponse,
  Part,
  SendMessageRequest,
  SendMessageResponse,
  SSEWritable,
  TaskArtifactUpdateEvent,
  TaskEventListener,
  TaskExecutor,
  TaskStatus as A2ATaskStatus,
  TaskStatusUpdateEvent,
  TextPart,
} from './a2a/index.js'
// A2A (Agent-to-Agent protocol) — named exports to avoid TaskStatus collision
export {
  A2A_PROTOCOL_VERSION,
  A2AServer,
  agentMessage,
  checkAuth,
  createAgentCard,
  createArtifactEvent,
  createStatusEvent,
  DEFAULT_A2A_PORT,
  DEFAULT_AGENT_VERSION,
  dataPart,
  extractBearerToken,
  formatJsonRpcSSE,
  formatSSE,
  getA2AServer,
  resetA2AServer,
  SSE_HEADERS,
  SSEWriter,
  setA2AServer,
  startKeepalive,
  statusEvent,
  TaskManager as A2ATaskManager,
  textPart,
  userMessage,
  validateBearerToken,
} from './a2a/index.js'
// ACP (Agent Client Protocol) integration
export * from './acp/index.js'
// Agent system (autonomous loop)
export * from './agent/index.js'
// Authentication (API key + OAuth)
export * from './auth/index.js'
// Message Bus (pub/sub)
export * from './bus/index.js'
// Codebase understanding
export * from './codebase/index.js'
// Commander (hierarchical delegation)
export * from './commander/index.js'
// Configuration (settings, credentials)
export * from './config/index.js'
// Context management (token tracking, compaction)
export * from './context/index.js'
// Custom Commands (TOML-based)
export * from './custom-commands/index.js'
// Diff tracking
export * from './diff/index.js'
// Extensions/Plugins
export * from './extensions/index.js'
// Focus Chain (task progress tracking)
export * from './focus-chain/index.js'
// Git snapshots
export * from './git/index.js'
// Hooks (tool lifecycle hooks)
export * from './hooks/index.js'
// Instructions (project/directory instructions)
export * from './instructions/index.js'
// External integrations (Exa, etc.)
export * from './integrations/index.js'
// LLM client
export * from './llm/index.js'
// LSP (Language Server Protocol) integration
export * from './lsp/index.js'
// MCP (Model Context Protocol) client
export * from './mcp/index.js'
// Memory system (long-term, RAG)
export * from './memory/index.js'
// Model registry
export * from './models/index.js'
// Permission system
export * from './permissions/index.js'
// Platform abstraction
export * from './platform.js'
export type {
  PolicyDecisionResult,
  PolicyDecisionType,
  PolicyEngineConfig,
  PolicyRule,
  SafetyChecker,
} from './policy/index.js'
// Policy Engine (tool approval rules) — named exports to avoid BUILTIN_RULES collision
export {
  ApprovalMode,
  BUILTIN_RULES as POLICY_BUILTIN_RULES,
  checkCompoundCommand,
  extractCommandName,
  getPolicyEngine,
  matchArgs,
  matchToolName,
  PolicyEngine,
  resetPolicyEngine,
  setPolicyEngine,
  stableStringify,
} from './policy/index.js'
// Question system (LLM-to-user questions)
export * from './question/index.js'
// Scheduler (background tasks)
export * from './scheduler/index.js'
// Session management
export * from './session/index.js'
// Skills (reusable knowledge modules)
export * from './skills/index.js'
// Slash Commands (user-invocable commands)
export * from './slash-commands/index.js'
// Tools
export * from './tools/index.js'
// Types
export * from './types/index.js'
// Validator (QA verification gate)
export * from './validator/index.js'
