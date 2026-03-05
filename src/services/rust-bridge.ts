import { invoke } from '@tauri-apps/api/core'
import type {
  BrowserToolResult,
  ComputeGrepResult,
  CopilotDeviceCodeResponse,
  CopilotDevicePollResponse,
  ExtensionRegistrationResult,
  FuzzyReplaceResult,
  GitToolResult,
  JsonValue,
  NativeExtensionRegistration,
  OAuthCallback,
  PermissionResult,
  PermissionRule,
  PluginStateEntry,
  PluginStateMap,
  PtySpawnOptions,
  ReflectResult,
  ReflectToolResult,
  RepoMapInputFile,
  RepoMapResult,
  RetryOutcome,
  RustMemoryEntry,
  RustSession,
  RustToolInfo,
  RustValidationResult,
  ToolResult,
  WasmExtensionRegistration,
} from '../types/rust-ipc'

const DEFAULT_DB_PATH = 'ava.db'

function toErrorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error)
}

async function invokeCommand<T>(command: string, args?: Record<string, unknown>): Promise<T> {
  try {
    return args ? await invoke<T>(command, args) : await invoke<T>(command)
  } catch (error) {
    throw new Error(`[rust-bridge:${command}] ${toErrorMessage(error)}`)
  }
}

export type MemoryEntry = RustMemoryEntry
export type ValidationResult = RustValidationResult
export type ToolInfo = RustToolInfo
export type AgentSession = RustSession

export const rustMemory = {
  remember: (key: string, value: string, dbPath = DEFAULT_DB_PATH): Promise<RustMemoryEntry> =>
    invokeCommand('memory_remember', { dbPath, key, value }),
  recall: (key: string, dbPath = DEFAULT_DB_PATH): Promise<RustMemoryEntry | null> =>
    invokeCommand('memory_recall', { dbPath, key }),
  search: (query: string, dbPath = DEFAULT_DB_PATH): Promise<RustMemoryEntry[]> =>
    invokeCommand('memory_search', { dbPath, query }),
  recent: (limit: number, dbPath = DEFAULT_DB_PATH): Promise<RustMemoryEntry[]> =>
    invokeCommand('memory_recent', { dbPath, limit }),
}

export const rustPermissions = {
  evaluate: (
    workspaceRoot: string,
    rules: PermissionRule[],
    tool: string,
    args: string[]
  ): Promise<PermissionResult> =>
    invokeCommand('evaluate_permission', { workspaceRoot, rules, tool, args }),
}

export const rustValidation = {
  validateEdit: (content: string): Promise<RustValidationResult> =>
    invokeCommand('validation_validate_edit', { content }),
  validateWithRetry: (
    content: string,
    candidateFixes: string[],
    maxAttempts = 3
  ): Promise<RetryOutcome> =>
    invokeCommand('validation_validate_with_retry', { content, maxAttempts, candidateFixes }),
}

export const rustGit = {
  execute: (payload: string): Promise<GitToolResult> =>
    invokeCommand('execute_git_tool', { payload }),
}

export const rustBrowser = {
  execute: (payload: string): Promise<BrowserToolResult> =>
    invokeCommand('execute_browser_tool', { payload }),
}

export const rustTools = {
  list: (): Promise<RustToolInfo[]> => invokeCommand('list_tools'),
  execute: (tool: string, args: Record<string, JsonValue>): Promise<ToolResult> =>
    invokeCommand('execute_tool', { tool, args }),
}

export const rustAgent = {
  run: (goal: string): Promise<RustSession> => invokeCommand('agent_run', { goal }),
  stream: (goal: string): Promise<void> => invokeCommand('agent_stream', { goal }),
}

export const rustCompute = {
  grep: (
    path: string,
    pattern: string,
    options?: { include?: string; maxResults?: number }
  ): Promise<ComputeGrepResult> =>
    invokeCommand('compute_grep', {
      path,
      pattern,
      include: options?.include,
      maxResults: options?.maxResults,
    }),
  fuzzyReplace: (
    content: string,
    oldString: string,
    newString: string,
    replaceAll = false
  ): Promise<FuzzyReplaceResult> =>
    invokeCommand('compute_fuzzy_replace', { content, oldString, newString, replaceAll }),
  repoMap: (
    files: RepoMapInputFile[],
    options?: {
      query?: string
      limit?: number
      activeFiles?: string[]
      mentionedFiles?: string[]
      privateFiles?: string[]
    }
  ): Promise<RepoMapResult> =>
    invokeCommand('compute_repo_map', {
      files,
      query: options?.query ?? '',
      limit: options?.limit,
      activeFiles: options?.activeFiles,
      mentionedFiles: options?.mentionedFiles,
      privateFiles: options?.privateFiles,
    }),
}

export const rustReflection = {
  reflectAndFix: (
    result: ReflectToolResult,
    generatedFix?: string,
    executionResult?: ReflectToolResult
  ): Promise<ReflectResult> =>
    invokeCommand('reflection_reflect_and_fix', { result, generatedFix, executionResult }),
}

export const rustPty = {
  spawn: ({ id, cols, rows, cwd }: PtySpawnOptions): Promise<void> =>
    invokeCommand('pty_spawn', { id, cols, rows, cwd }),
  write: (id: string, data: string): Promise<void> => invokeCommand('pty_write', { id, data }),
  resize: (id: string, cols: number, rows: number): Promise<void> =>
    invokeCommand('pty_resize', { id, cols, rows }),
  kill: (id: string): Promise<void> => invokeCommand('pty_kill', { id }),
}

export const rustOAuth = {
  listen: (port: number): Promise<OAuthCallback> => invokeCommand('oauth_listen', { port }),
  copilotDeviceStart: (clientId: string, scope: string): Promise<CopilotDeviceCodeResponse> =>
    invokeCommand('oauth_copilot_device_start', { clientId, scope }),
  copilotDevicePoll: (clientId: string, deviceCode: string): Promise<CopilotDevicePollResponse> =>
    invokeCommand('oauth_copilot_device_poll', { clientId, deviceCode }),
}

export const rustSystem = {
  getEnvVar: (name: string): Promise<string | null> => invokeCommand('get_env_var', { name }),
  getCwd: (): Promise<string> => invokeCommand('get_cwd'),
  appendLog: (path: string, content: string): Promise<void> =>
    invokeCommand('append_log', { path, content }),
  cleanupOldLogs: (dir: string, maxAgeDays: number): Promise<number> =>
    invokeCommand('cleanup_old_logs', { dir, maxAgeDays }),
  allowProjectPath: (path: string): Promise<void> => invokeCommand('allow_project_path', { path }),
}

export const rustPlugins = {
  getState: (): Promise<PluginStateMap> => invokeCommand('get_plugins_state'),
  setState: (state: PluginStateMap): Promise<void> => invokeCommand('set_plugins_state', { state }),
  install: (pluginId: string): Promise<PluginStateEntry> =>
    invokeCommand('install_plugin', { pluginId }),
  uninstall: (pluginId: string): Promise<PluginStateEntry> =>
    invokeCommand('uninstall_plugin', { pluginId }),
  setEnabled: (pluginId: string, enabled: boolean): Promise<PluginStateEntry> =>
    invokeCommand('set_plugin_enabled', { pluginId, enabled }),
}

export const rustExtensions = {
  registerNative: (input: NativeExtensionRegistration): Promise<ExtensionRegistrationResult> =>
    invokeCommand('extensions_register_native', { ...input }),
  registerWasm: (input: WasmExtensionRegistration): Promise<ExtensionRegistrationResult> =>
    invokeCommand('extensions_register_wasm', { ...input }),
}
