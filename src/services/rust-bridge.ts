import { isTauri, invoke as tauriInvoke } from '@tauri-apps/api/core'
import { apiInvoke } from '../lib/api-client'
import type { PluginHostInvokeResult, PluginMountRegistration } from '../types/plugin'
import type {
  ActiveSessionSyncResult,
  ActiveSessionSyncSnapshot,
  AgentStatus,
  AgentToolInfo,
  BrowserToolResult,
  ClearTarget,
  CompactContextResult,
  CompactMessage,
  ComputeGrepResult,
  CopilotDeviceCodeResponse,
  CopilotDevicePollResponse,
  CurrentModel,
  EditAndResendArgs,
  ExtensionRegistrationResult,
  FuzzyReplaceResult,
  GitToolResult,
  InstalledPluginInfo,
  JsonValue,
  McpReloadResult,
  McpServerInfo,
  MessageQueueState,
  ModelInfo,
  NativeExtensionRegistration,
  OAuthCallback,
  PermissionLevelInfo,
  PermissionLevelValue,
  PermissionResult,
  PermissionRule,
  PluginStateEntry,
  PluginStateMap,
  ProviderInfo,
  PtySpawnOptions,
  ReflectResult,
  ReflectToolResult,
  RepoMapInputFile,
  RepoMapResult,
  RetryOutcome,
  RunCorrelationArgs,
  RustMemoryEntry,
  RustSession,
  RustToolInfo,
  RustValidationResult,
  SessionSummary,
  SubmitGoalArgs,
  SubmitGoalResult,
  ToolIntrospectionContext,
  ToolResult,
  UndoResult,
  WasmExtensionRegistration,
} from '../types/rust-ipc'

const DEFAULT_DB_PATH = 'ava.db'

function toErrorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error)
}

function toSnakeCaseValue(value: unknown): unknown {
  if (Array.isArray(value)) {
    return value.map((entry) => toSnakeCaseValue(entry))
  }
  if (value && typeof value === 'object') {
    const normalized: Record<string, unknown> = {}
    for (const [key, entry] of Object.entries(value)) {
      normalized[key.replace(/[A-Z]/g, (c) => `_${c.toLowerCase()}`)] = toSnakeCaseValue(entry)
    }
    return normalized
  }
  return value
}

async function invokeCommand<T>(command: string, args?: Record<string, unknown>): Promise<T> {
  try {
    if (isTauri()) {
      return args ? await tauriInvoke<T>(command, args) : await tauriInvoke<T>(command)
    }
    return await apiInvoke<T>(command, args)
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
  // All agent execution is inherently streaming via the 'agent-event' Tauri event channel.
  // The return value is only the final session summary; real-time output arrives via events.
  run: (goal: string): Promise<SubmitGoalResult> =>
    invokeCommand('submit_goal', { args: { goal } }),
  cancel: (correlation?: RunCorrelationArgs): Promise<void> =>
    invokeCommand('cancel_agent', correlation ? { args: correlation } : undefined),
  status: (correlation?: RunCorrelationArgs): Promise<AgentStatus> =>
    invokeCommand('get_agent_status', correlation ? { args: correlation } : undefined),
  resolveApproval: (requestId: string, approved: boolean, alwaysAllow: boolean): Promise<void> =>
    invokeCommand('resolve_approval', { args: { requestId, approved, alwaysAllow } }),
  resolveQuestion: (requestId: string, answer: string): Promise<void> =>
    invokeCommand('resolve_question', { args: { requestId, answer } }),
  resolvePlan: (
    requestId: string,
    response: import('../types/rust-ipc').PlanResponse,
    modifiedPlan: import('../types/rust-ipc').PlanData | null,
    feedback?: string | null
  ): Promise<void> =>
    invokeCommand('resolve_plan', {
      args: {
        requestId,
        response,
        modifiedPlan: modifiedPlan ? toSnakeCaseValue(modifiedPlan as unknown) : null,
        feedback: feedback ?? null,
      },
    }),
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

export const rustBackend = {
  submitGoal: (args: SubmitGoalArgs): Promise<SubmitGoalResult> =>
    invokeCommand('submit_goal', { args }),
  cancelAgent: (correlation?: RunCorrelationArgs): Promise<void> =>
    invokeCommand('cancel_agent', correlation ? { args: correlation } : undefined),
  getAgentStatus: (correlation?: RunCorrelationArgs): Promise<AgentStatus> =>
    invokeCommand('get_agent_status', correlation ? { args: correlation } : undefined),

  listSessions: (limit?: number): Promise<SessionSummary[]> =>
    invokeCommand('list_sessions', { limit }),
  loadSession: (id: string): Promise<JsonValue> => invokeCommand('load_session', { id }),
  createSession: (): Promise<SessionSummary> => invokeCommand('create_session'),
  deleteSession: (id: string): Promise<void> => invokeCommand('delete_session', { id }),
  setActiveSession: (
    id: string,
    workingDirectory?: string,
    snapshot?: ActiveSessionSyncSnapshot
  ): Promise<ActiveSessionSyncResult> =>
    invokeCommand(
      'set_active_session',
      snapshot
        ? { id, workingDirectory, snapshot }
        : workingDirectory !== undefined
          ? { id, workingDirectory }
          : { id }
    ),

  listModels: (): Promise<ModelInfo[]> => invokeCommand('list_models'),
  getCurrentModel: (): Promise<CurrentModel> => invokeCommand('get_current_model'),
  switchModel: (provider: string, model: string): Promise<void> =>
    invokeCommand('switch_model', { provider, model }),

  listProviders: (): Promise<ProviderInfo[]> => invokeCommand('list_providers'),

  getConfig: (): Promise<JsonValue> => invokeCommand('get_config'),
  setPrimaryAgentProfile: (prompt: string | null): Promise<void> =>
    invokeCommand('set_primary_agent_profile', { prompt }),

  listAgentTools: (context?: ToolIntrospectionContext): Promise<AgentToolInfo[]> =>
    invokeCommand('list_agent_tools', { context }),

  listMcpServers: (): Promise<McpServerInfo[]> => invokeCommand('list_mcp_servers'),
  reloadMcpServers: (): Promise<McpReloadResult> => invokeCommand('reload_mcp_servers'),
  enableMcpServer: (name: string): Promise<void> => invokeCommand('enable_mcp_server', { name }),
  disableMcpServer: (name: string): Promise<void> => invokeCommand('disable_mcp_server', { name }),

  listInstalledPlugins: (): Promise<InstalledPluginInfo[]> =>
    invokeCommand('list_installed_plugins'),
  listPluginMounts: (): Promise<PluginMountRegistration[]> => invokeCommand('list_plugin_mounts'),
  pluginHostInvoke: (
    plugin: string,
    command: string,
    payload: Record<string, unknown> = {}
  ): Promise<PluginHostInvokeResult> =>
    invokeCommand('plugin_host_invoke', { args: { plugin, command, payload } }),

  getPermissionLevel: (): Promise<PermissionLevelInfo> => invokeCommand('get_permission_level'),
  setPermissionLevel: (level: PermissionLevelValue): Promise<PermissionLevelInfo> =>
    invokeCommand('set_permission_level', { level }),
  togglePermissionLevel: (): Promise<PermissionLevelInfo> =>
    invokeCommand('toggle_permission_level'),

  // Mid-stream messaging (3-tier)
  steerAgent: (message: string, correlation?: RunCorrelationArgs): Promise<void> =>
    invokeCommand(
      'steer_agent',
      isTauri()
        ? { args: { message, ...(correlation ?? {}) } }
        : correlation
          ? { message, ...correlation }
          : { message }
    ),
  followUpAgent: (
    message: string,
    sessionId?: string,
    correlation?: RunCorrelationArgs
  ): Promise<void> =>
    invokeCommand('follow_up_agent', {
      args: { message, sessionId: sessionId ?? null, ...(correlation ?? {}) },
    }),
  postCompleteAgent: (
    message: string,
    group?: number,
    sessionId?: string,
    correlation?: RunCorrelationArgs
  ): Promise<void> =>
    invokeCommand('post_complete_agent', {
      args: { message, group: group ?? 1, sessionId: sessionId ?? null, ...(correlation ?? {}) },
    }),
  getMessageQueue: (correlation?: RunCorrelationArgs): Promise<MessageQueueState> =>
    invokeCommand('get_message_queue', correlation ? { args: correlation } : undefined),
  clearMessageQueue: (
    target: ClearTarget = 'all',
    correlation?: RunCorrelationArgs
  ): Promise<void> =>
    invokeCommand(
      'clear_message_queue',
      isTauri() ? { args: { target, ...(correlation ?? {}) } } : { target, ...(correlation ?? {}) }
    ),

  // Retry / Edit+Resend / Regenerate / Undo
  retryLastMessage: (sessionId?: string): Promise<SubmitGoalResult> =>
    invokeCommand('retry_last_message', { args: { sessionId: sessionId ?? null } }),
  editAndResend: (args: EditAndResendArgs): Promise<SubmitGoalResult> =>
    invokeCommand('edit_and_resend', { args }),
  regenerateResponse: (sessionId?: string): Promise<SubmitGoalResult> =>
    invokeCommand('regenerate_response', { args: { sessionId: sessionId ?? null } }),
  undoLastEdit: (correlation?: RunCorrelationArgs): Promise<UndoResult> =>
    invokeCommand('undo_last_edit', correlation ? { args: correlation } : undefined),

  // Session rename/search
  renameSession: (id: string, title: string): Promise<void> =>
    invokeCommand('rename_session', { id, title }),
  searchSessions: (query: string): Promise<SessionSummary[]> =>
    invokeCommand('search_sessions', { query }),

  // Context compaction
  compactContext: (
    messages: CompactMessage[],
    focus?: string,
    contextWindow?: number,
    sessionId?: string,
    compactionProvider?: string,
    compactionModel?: string
  ): Promise<CompactContextResult> =>
    invokeCommand('compact_context', {
      messages,
      focus: focus ?? null,
      contextWindow: contextWindow ?? null,
      sessionId: sessionId ?? null,
      compactionProvider: compactionProvider ?? null,
      compactionModel: compactionModel ?? null,
    }),
}
