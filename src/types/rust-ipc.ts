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
  id?: string
  name: string
  args?: Record<string, JsonValue>
  arguments?: Record<string, JsonValue>
}
export interface ToolResultEvent {
  type: 'tool_result'
  call_id?: string
  callId?: string
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

export interface ContextCompactedEvent {
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
  type: 'approval_request'
  id: string
  tool_call_id?: string
  toolCallId?: string
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

// ── HQ multi-agent events ──────────────────────────────────────

export interface HqWorkerStartedEvent {
  type: 'hq_worker_started'
  worker_id: string
  lead: string
  task: string
}

export interface HqWorkerProgressEvent {
  type: 'hq_worker_progress'
  worker_id: string
  turn: number
  max_turns: number
}

export interface HqWorkerTokenEvent {
  type: 'hq_worker_token'
  worker_id: string
  token: string
}

export interface HqWorkerThinkingEvent {
  type: 'hq_worker_thinking'
  worker_id: string
  content: string
}

export interface HqWorkerToolCallEvent {
  type: 'hq_worker_tool_call'
  worker_id: string
  call_id: string
  name: string
  args: Record<string, JsonValue>
}

export interface HqWorkerToolResultEvent {
  type: 'hq_worker_tool_result'
  worker_id: string
  call_id: string
  content: string
  is_error: boolean
}

export interface HqWorkerCompletedEvent {
  type: 'hq_worker_completed'
  worker_id: string
  success: boolean
  turns: number
}

export interface HqWorkerFailedEvent {
  type: 'hq_worker_failed'
  worker_id: string
  error: string
}

export interface HqAllCompleteEvent {
  type: 'hq_all_complete'
  total_workers: number
  succeeded: number
  failed: number
}

export interface HqSummaryEvent {
  type: 'hq_summary'
  total_workers: number
  succeeded: number
  failed: number
  total_turns: number
}

export interface HqPhaseStartedEvent {
  type: 'hq_phase_started'
  phase_index: number
  phase_count: number
  phase_name: string
  role: string
}

export interface HqPhaseCompletedEvent {
  type: 'hq_phase_completed'
  phase_index: number
  phase_name: string
  turns: number
  output_preview: string
}

export interface HqSpecCreatedEvent {
  type: 'hq_spec_created'
  spec_id: string
  title: string
}

export interface HqArtifactCreatedEvent {
  type: 'hq_artifact_created'
  artifact_id: string
  kind: string
  producer: string
  title: string
}

export interface HqConflictDetectedEvent {
  type: 'hq_conflict_detected'
  workers: [string, string]
  overlapping_files: string[]
}

export interface HqExternalWorkerStartedEvent {
  type: 'hq_external_worker_started'
  worker_id: string
  agent_name: string
}

export interface HqExternalWorkerCompletedEvent {
  type: 'hq_external_worker_completed'
  worker_id: string
  success: boolean
  cost_usd?: number
}

export interface HqExternalWorkerFailedEvent {
  type: 'hq_external_worker_failed'
  worker_id: string
  error: string
}

// ── Plan events ──────────────────────────────────────────────────────

export type PlanStepAction = 'research' | 'implement' | 'test' | 'review'

export interface PlanStep {
  id: string
  description: string
  files: string[]
  action: PlanStepAction
  dependsOn: string[]
  approved: boolean
}

export interface PlanData {
  summary: string
  steps: PlanStep[]
  estimatedTurns: number
  estimatedBudgetUsd?: number
  codename?: string
}

export interface PlanSummary {
  filename: string
  codename: string | null
  summary: string
  stepCount: number
  created: string
}

export interface PlanCreatedEvent {
  type: 'plan_created'
  plan: PlanData
}

export interface PlanStepCompleteEvent {
  type: 'plan_step_complete'
  step_id: string
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
  type: 'todo_update'
  todos: TodoItem[]
}

export interface ResolvePlanArgs {
  response: PlanResponse
  modifiedPlan?: PlanData | null
  feedback?: string | null
  stepComments?: Record<string, string> | null
}

export type HqEvent =
  | HqWorkerStartedEvent
  | HqWorkerProgressEvent
  | HqWorkerTokenEvent
  | HqWorkerThinkingEvent
  | HqWorkerToolCallEvent
  | HqWorkerToolResultEvent
  | HqWorkerCompletedEvent
  | HqWorkerFailedEvent
  | HqAllCompleteEvent
  | HqSummaryEvent
  | HqPhaseStartedEvent
  | HqPhaseCompletedEvent
  | HqSpecCreatedEvent
  | HqArtifactCreatedEvent
  | HqConflictDetectedEvent
  | HqExternalWorkerStartedEvent
  | HqExternalWorkerCompletedEvent
  | HqExternalWorkerFailedEvent

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
  | PlanCreatedEvent
  | PlanStepCompleteEvent
  | TodoUpdateEvent
  | HqEvent

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
  thinkingLevel?: string
  sessionId?: string
  autoCompact?: boolean
  compactionThreshold?: number
  compactionProvider?: string
  compactionModel?: string
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
  reasoning: boolean
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

export type LspRuntimeState = 'disabled' | 'idle' | 'starting' | 'ready' | 'error' | 'unavailable'

export interface LspDiagnosticSummary {
  errors: number
  warnings: number
  info: number
}

export interface LspServerSnapshot {
  name: string
  state: LspRuntimeState
  active: boolean
  relevant: boolean
  diagnostics: LspDiagnosticSummary
  lastError?: string | null
}

export interface LspSuggestion {
  server: string
  title: string
  message: string
  frameworks: string[]
  installProfile?: string | null
  installCommand?: string | null
  key: string
}

export interface LspInstallResult {
  profile: string
  command: string
  success: boolean
  message: string
}

export interface LspStatusSnapshot {
  enabled: boolean
  mode: string
  activeServerCount: number
  summary: LspDiagnosticSummary
  servers: LspServerSnapshot[]
  suggestions: LspSuggestion[]
}

export interface McpServerInfo {
  name: string
  toolCount: number
  scope: string
  enabled: boolean
  /** Connection status: "connected" | "disabled" | "failed" | "connecting" */
  status?: string
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

// HQ multi-agent IPC types

export interface LeadConfigPayload {
  domain: string
  enabled: boolean
  model: string
  maxWorkers: number
  customPrompt?: string
}

export interface TeamConfigPayload {
  defaultDirectorModel: string
  defaultLeadModel: string
  defaultWorkerModel: string
  defaultScoutModel: string
  workerNames: string[]
  leads: LeadConfigPayload[]
}

export interface StartHqArgs {
  goal: string
  domain?: string
  teamConfig?: TeamConfigPayload
}

export interface HqStatusResult {
  running: boolean
  totalWorkers: number
  succeeded: number
  failed: number
}

// Subscription usage types

export interface UsageWindow {
  label: string
  usedPercent: number
  resetsAt: string | null
}

export interface CreditsInfo {
  hasCredits: boolean
  unlimited: boolean
  balance: string | null
}

export interface CopilotQuota {
  remaining: number
  limit: number
  percentRemaining: number
  resetTime: string | null
  completionsRemaining: number | null
  completionsLimit: number | null
}

export interface SubscriptionUsage {
  provider: string
  displayName: string
  planType: string | null
  usageWindows: UsageWindow[]
  credits: CreditsInfo | null
  copilotQuota: CopilotQuota | null
  error: string | null
}
