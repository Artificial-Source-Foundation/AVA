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

export interface ToolIntrospectionMessageContext {
  role: 'user' | 'assistant' | 'system'
  content: string
  agentVisible?: boolean
}

export interface ToolIntrospectionImageContext {
  data: string
  mediaType: string
}

export interface ToolIntrospectionContext {
  sessionId?: string
  goal?: string
  history?: ToolIntrospectionMessageContext[]
  images?: ToolIntrospectionImageContext[]
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

export interface ActiveSessionSyncResult {
  sessionId: string
  exists: boolean
  messageCount: number
}

export interface ActiveSessionSyncMessageSnapshot {
  id: string
  role: 'user' | 'assistant' | 'system' | 'tool'
  content: string
  createdAt: number
  images?: ActiveSessionSyncImageSnapshot[]
  metadata?: Record<string, unknown>
}

export interface ActiveSessionSyncImageSnapshot {
  data: string
  mediaType: string
}

export interface ActiveSessionSyncSnapshot {
  title?: string
  messages: ActiveSessionSyncMessageSnapshot[]
}

export interface TokenEvent {
  run_id?: string
  runId?: string
  type: 'token'
  content: string
}
export interface ToolCallEvent {
  run_id?: string
  runId?: string
  type: 'tool_call'
  id?: string
  name: string
  args?: Record<string, JsonValue>
  arguments?: Record<string, JsonValue>
}
export interface ToolResultEvent {
  run_id?: string
  runId?: string
  type: 'tool_result'
  call_id?: string
  callId?: string
  content: string
  is_error: boolean
}
export interface ProgressEvent {
  run_id?: string
  runId?: string
  type: 'progress'
  message: string
}
export interface CompleteEvent {
  type: 'complete'
  session: RustSession
  run_id?: string
  runId?: string
}
export interface ErrorEvent {
  type: 'error'
  message: string
  run_id?: string
  runId?: string
}

export interface ThinkingEvent {
  run_id?: string
  runId?: string
  type: 'thinking'
  content: string
}

export interface TokenUsageEvent {
  run_id?: string
  runId?: string
  type: 'token_usage'
  inputTokens: number
  outputTokens: number
  costUsd: number
}

export interface BudgetWarningEvent {
  run_id?: string
  runId?: string
  type: 'budget_warning'
  thresholdPercent: number
  currentCostUsd: number
  maxBudgetUsd: number
}

export interface ContextCompactedEvent {
  run_id?: string
  runId?: string
  type: 'context_compacted'
  auto: boolean
  tokensBefore: number
  tokensAfter: number
  tokensSaved: number
  messagesBefore: number
  messagesAfter: number
  usageBeforePercent: number
  summary: string
  contextSummary: string
  activeMessages: CompactMessageOut[]
}

export interface ApprovalRequestEvent {
  run_id?: string
  runId?: string
  type: 'approval_request'
  id: string
  tool_call_id?: string
  tool_name: string
  args: Record<string, JsonValue>
  risk_level: string
  reason: string
  warnings: string[]
}

export interface QuestionRequestEvent {
  run_id?: string
  runId?: string
  type: 'question_request'
  id: string
  question: string
  options: string[]
}

export interface InteractiveRequestClearedEvent {
  run_id?: string
  runId?: string
  type: 'interactive_request_cleared'
  request_id?: string
  request_kind?: 'approval' | 'question' | 'plan'
  timed_out?: boolean
}

// ── Plan events ──────────────────────────────────────────────────────

export type PlanStepAction = 'research' | 'implement' | 'test' | 'review'

export interface PlanStep {
  id: string
  description: string
  files: string[]
  action: PlanStepAction
  dependsOn: string[]
  approved?: boolean
}

export interface PlanData {
  summary: string
  steps: PlanStep[]
  estimatedTurns: number
  estimatedBudgetUsd?: number
  codename?: string
  requestId?: string
}

export interface PlanSummary {
  filename: string
  codename: string | null
  summary: string
  stepCount: number
  created: string
}

export interface PlanCreatedEvent {
  run_id?: string
  runId?: string
  type: 'plan_created'
  id?: string
  plan: PlanData
}

export interface PlanStepCompleteEvent {
  run_id: string
  runId?: string
  type: 'plan_step_complete'
  step_id: string
}

export interface StreamingEditProgressEvent {
  run_id: string
  runId?: string
  type: 'streaming_edit_progress'
  call_id: string
  tool_name: string
  file_path?: string | null
  bytes_received: number
}

export interface SubagentCompleteEvent {
  run_id: string
  runId?: string
  type: 'subagent_complete'
  call_id: string
  session_id: string
  description: string
  input_tokens: number
  output_tokens: number
  cost_usd: number
  agent_type?: string | null
  provider?: string | null
  resumed: boolean
}

export type PlanResponse = 'approved' | 'rejected' | 'modified'

// ── Todo events ───────────────────────────────────────────────────────

export type TodoStatus = 'pending' | 'in_progress' | 'completed' | 'cancelled'
export type TodoPriority = 'high' | 'medium' | 'low'

export interface TodoItem {
  content: string
  status: TodoStatus
  priority: TodoPriority
}

export interface TodoUpdateEvent {
  run_id?: string
  runId?: string
  type: 'todo_update'
  todos: TodoItem[]
}

export interface ResolvePlanArgs {
  requestId: string
  response: PlanResponse
  modifiedPlan?: PlanData | null
  feedback?: string | null
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
  | ContextCompactedEvent
  | ApprovalRequestEvent
  | QuestionRequestEvent
  | InteractiveRequestClearedEvent
  | PlanCreatedEvent
  | PlanStepCompleteEvent
  | StreamingEditProgressEvent
  | SubagentCompleteEvent
  | TodoUpdateEvent

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
  runId?: string
  maxTurns?: number
  provider?: string
  model?: string
  thinkingLevel?: string
  sessionId?: string
  images?: ToolIntrospectionImageContext[]
  autoCompact?: boolean
  compactionThreshold?: number
  compactionProvider?: string
  compactionModel?: string
}

export interface SubmitGoalResult {
  // In accepted-and-streaming paths this is only the accepted run handle shape.
  // Terminal success/failure comes from streamed events, not these fields.
  success: boolean
  turns: number
  sessionId: string
  detachedSessionId?: string | null
}

export interface AgentStatus {
  running: boolean
  provider: string
  model: string
  runId?: string | null
  pendingApproval?: ApprovalRequestEvent | null
  pendingQuestion?: QuestionRequestEvent | null
  pendingPlan?: PlanCreatedEvent | null
}

export interface RunCorrelationArgs {
  runId?: string
  sessionId?: string
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
  reasoning: boolean
  capabilities: string[]
  contextWindow: number
  maxOutput?: number | null
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

export interface CLIAgentInfo {
  name: string
  binary: string
  version: string
  installed: boolean
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
  canToggle: boolean
  /** Connection status: "connected" | "disabled" | "failed" | "connecting" */
  status: string
  error?: string
}

export interface McpReloadResult {
  serverCount: number
  toolCount: number
}

export type PluginStatusValue = 'running' | 'stopped' | 'failed'

export interface InstalledPluginInfo {
  name: string
  version: string
  status: PluginStatusValue
  hooks: string[]
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
  runId?: string
  sessionId?: string
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
  contextSummary: string
  usageBeforePercent: number
}
