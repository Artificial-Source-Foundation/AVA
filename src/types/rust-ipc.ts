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

export type AgentEvent =
  | TokenEvent
  | ToolCallEvent
  | ToolResultEvent
  | ProgressEvent
  | CompleteEvent
  | ErrorEvent

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
