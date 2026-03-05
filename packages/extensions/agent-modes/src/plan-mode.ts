/**
 * Plan mode — restricts tool usage to read-only operations.
 *
 * When active, the agent can only research and read, not modify files.
 */

import type { AgentMode, Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { ToolDefinition } from '@ava/core-v2/llm'

export interface PlanModeState {
  enabled: boolean
  enteredAt?: Date
  reason?: string
}

const ALLOWED_TOOLS = new Set([
  'read_file',
  'glob',
  'grep',
  'ls',
  'websearch',
  'webfetch',
  'todoread',
  'plan_exit',
  'attempt_completion',
])

const states = new Map<string, PlanModeState>()

export function enterPlanMode(sessionId: string, reason?: string): void {
  states.set(sessionId, { enabled: true, enteredAt: new Date(), reason })
}

export function exitPlanMode(sessionId: string): void {
  states.delete(sessionId)
}

export function isPlanModeEnabled(sessionId: string): boolean {
  return states.get(sessionId)?.enabled ?? false
}

export function getPlanModeState(sessionId: string): PlanModeState | undefined {
  return states.get(sessionId)
}

export function resetPlanMode(): void {
  states.clear()
}

export function isToolAllowedInPlanMode(toolName: string): boolean {
  return ALLOWED_TOOLS.has(toolName)
}

export const planAgentMode: AgentMode = {
  name: 'plan',
  description: 'Research-only mode: restricts tools to read operations',

  filterTools(tools: ToolDefinition[]): ToolDefinition[] {
    return tools.filter((t) => ALLOWED_TOOLS.has(t.name))
  },

  systemPrompt(base: string): string {
    return `${base}\n\nYou are in PLAN MODE. You can only research and read — no file modifications allowed. Use read_file, glob, grep, ls, and web tools to gather information. Call plan_exit when ready to execute.`
  },
}

export function registerPlanMode(api: ExtensionAPI): Disposable {
  return api.registerAgentMode(planAgentMode)
}
