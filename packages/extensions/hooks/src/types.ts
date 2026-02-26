/**
 * Hook types — lifecycle hook contracts.
 */

export type HookType = 'PreToolUse' | 'PostToolUse' | 'TaskStart' | 'TaskComplete' | 'TaskCancel'

export interface HookResult {
  cancel?: boolean
  contextModification?: string
  errorMessage?: string
}

export interface PreToolUseContext {
  toolName: string
  parameters: Record<string, unknown>
  workingDirectory: string
  sessionId: string
}

export interface PostToolUseContext {
  toolName: string
  parameters: Record<string, unknown>
  result: string
  success: boolean
  durationMs: number
  workingDirectory: string
  sessionId: string
}

export interface TaskStartContext {
  goal: string
  sessionId: string
  workingDirectory: string
}

export interface TaskCompleteContext {
  success: boolean
  output: string
  durationMs: number
  toolCallCount: number
}

export interface TaskCancelContext {
  reason: string
  sessionId: string
  workingDirectory: string
  durationMs: number
}

export type HookContext =
  | PreToolUseContext
  | PostToolUseContext
  | TaskStartContext
  | TaskCompleteContext
  | TaskCancelContext

export interface HookConfig {
  timeout: number
  continueOnError: boolean
  workingDirectory?: string
}

export const DEFAULT_HOOK_CONFIG: HookConfig = {
  timeout: 30_000,
  continueOnError: true,
}

export interface RegisteredHook {
  type: HookType
  name: string
  handler: (context: HookContext) => Promise<HookResult>
}
