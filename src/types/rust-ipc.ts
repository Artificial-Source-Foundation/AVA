export type JsonValue =
  | string
  | number
  | boolean
  | null
  | JsonValue[]
  | { [key: string]: JsonValue }

export interface RustMemoryEntry {
  id: number
  key: string
  value: string
  createdAt: string
}

export type PermissionAction = 'allow' | 'deny' | 'ask'

export type PermissionPattern =
  | { type: 'any' }
  | { type: 'glob'; value: string }
  | { type: 'regex'; value: string }
  | { type: 'path'; value: string }

export interface PermissionRule {
  tool: PermissionPattern
  args: PermissionPattern
  action: PermissionAction
}
export interface PermissionResult {
  action: PermissionAction
}

export interface RustValidationResult {
  valid: boolean
  error?: string | null
  details: string[]
}
export interface RetryOutcome {
  result: RustValidationResult
  finalContent: string
  attempts: number
}

export interface GitToolResult {
  program: string
  args: string[]
  stdout: string
  stderr: string
  exitCode: number
}

export interface BrowserToolResult {
  output: string
}
export interface RustToolInfo {
  name: string
  description: string
}
export interface ToolResult {
  content: string
  is_error: boolean
}

export interface RustSession {
  id: string
  goal?: string
  messages: Record<string, JsonValue>[]
  completed: boolean
}

export interface TokenEvent {
  type: 'token'
  content: string
}
export interface ToolCallEvent {
  type: 'tool_call'
  name: string
  args: Record<string, JsonValue>
}
export interface ToolResultEvent {
  type: 'tool_result'
  content: string
  is_error: boolean
}
export interface ProgressEvent {
  type: 'progress'
  message: string
}
export interface CompleteEvent {
  type: 'complete'
  session: RustSession
}
export interface ErrorEvent {
  type: 'error'
  message: string
}

export interface ThinkingEvent {
  type: 'thinking'
  content: string
}

export interface TokenUsageEvent {
  type: 'token_usage'
  inputTokens: number
  outputTokens: number
  costUsd: number
}

export interface BudgetWarningEvent {
  type: 'budget_warning'
  thresholdPercent: number
  currentCostUsd: number
  maxBudgetUsd: number
}

export interface ApprovalRequestEvent {
  type: 'approval_request'
  id: string
  tool_name: string
  args: Record<string, JsonValue>
  risk_level: string
  reason: string
  warnings: string[]
}

export interface QuestionRequestEvent {
  type: 'question_request'
  id: string
  question: string
  options: string[]
}

export type AgentEvent =
  | TokenEvent
  | ToolCallEvent
  | ToolResultEvent
  | ProgressEvent
  | CompleteEvent
  | ErrorEvent
  | ThinkingEvent
  | TokenUsageEvent
  | BudgetWarningEvent
  | ApprovalRequestEvent
  | QuestionRequestEvent

export interface ComputeGrepMatch {
  file: string
  line: number
  content: string
}
export interface ComputeGrepResult {
  matches: ComputeGrepMatch[]
  truncated: boolean
}
export interface FuzzyReplaceResult {
  content: string
  strategy: string
}

export interface RepoMapInputFile {
  path: string
  content: string
  dependencies: string[]
}

export interface RepoMapEntry {
  path: string
  score: number
}

export interface RepoMapResult {
  files: RepoMapEntry[]
}

export interface ReflectToolResult {
  output: string
  error?: string | null
}
export interface ReflectResult {
  output: string
  error?: string | null
  attemptedFix: boolean
  errorKind?: 'syntax' | 'import' | 'type' | 'command' | 'unknown' | null
}

export interface PtySpawnOptions {
  id: string
  cols: number
  rows: number
  cwd?: string
}

export interface OAuthCallback {
  code: string
  state: string
}

export interface CopilotDeviceCodeResponse {
  device_code: string
  user_code: string
  verification_uri: string
  expires_in: number
  interval?: number | null
}

export interface CopilotDevicePollResponse {
  access_token?: string | null
  refresh_token?: string | null
  expires_in?: number | null
  error?: string | null
}

export interface PluginStateEntry {
  installed: boolean
  enabled: boolean
}
export type PluginStateMap = Record<string, PluginStateEntry>

export type ExtensionHookPoint = 'before_tool_execution' | 'after_tool_execution'
export interface NativeExtensionHook {
  name: string
  point: ExtensionHookPoint
}

export interface NativeExtensionRegistration {
  name: string
  version: string
  path: string
  tools: string[]
  hooks: NativeExtensionHook[]
  validators: string[]
}

export interface WasmExtensionRegistration {
  name: string
  version: string
  path: string
  tools: string[]
  hooks: ExtensionHookPoint[]
  validators: string[]
  metadata: Record<string, string>
}

export type ExtensionRegistrationResult =
  | ({ kind: 'native' } & NativeExtensionRegistration)
  | ({ kind: 'wasm' } & WasmExtensionRegistration)

// New types for the Rust backend bridge

export interface SubmitGoalArgs {
  goal: string
  maxTurns?: number
  provider?: string
  model?: string
}

export interface SubmitGoalResult {
  success: boolean
  turns: number
  sessionId: string
}

export interface AgentStatus {
  running: boolean
  provider: string
  model: string
}

export interface SessionSummary {
  id: string
  title: string
  messageCount: number
  createdAt: string
  updatedAt: string
}

export interface ModelInfo {
  id: string
  provider: string
  name: string
  toolCall: boolean
  vision: boolean
  contextWindow: number
  costInput: number
  costOutput: number
}

export interface CurrentModel {
  provider: string
  model: string
}

export interface ProviderInfo {
  name: string
}

export interface AgentToolInfo {
  name: string
  description: string
  source: string
}

export interface McpServerInfo {
  name: string
  toolCount: number
  scope: string
  enabled: boolean
}

export interface McpReloadResult {
  serverCount: number
  toolCount: number
}

export type PermissionLevelValue = 'standard' | 'autoApprove'

export interface PermissionLevelInfo {
  level: PermissionLevelValue
}

// Mid-stream messaging types (3-tier)

export interface PostCompleteArgs {
  message: string
  group?: number
}

export interface MessageQueueState {
  active: boolean
}

export type ClearTarget = 'all' | 'steering' | 'followUp' | 'postComplete'

// Retry / Edit+Resend / Undo types

export interface EditAndResendArgs {
  messageId: string
  newContent: string
}

export interface UndoResult {
  success: boolean
  message: string
  filePath: string | null
}

// Context compaction types

export interface CompactMessage {
  role: string
  content: string
}

export interface CompactMessageOut {
  role: string
  content: string
}

export interface CompactContextResult {
  messages: CompactMessageOut[]
  tokensBefore: number
  tokensAfter: number
  tokensSaved: number
  messagesBefore: number
  messagesAfter: number
  summary: string
}
