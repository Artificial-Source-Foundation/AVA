/**
 * Minimal mode — token-efficiency mode with core tools only.
 */

import type { AgentMode, Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { ToolDefinition } from '@ava/core-v2/llm'

const ALLOWED_TOOLS = new Set([
  'read_file',
  'write_file',
  'edit',
  'glob',
  'grep',
  'bash',
  'attempt_completion',
  'question',
])

const states = new Map<string, boolean>()

export function enterMinimalMode(sessionId: string): void {
  states.set(sessionId, true)
}

export function exitMinimalMode(sessionId: string): void {
  states.delete(sessionId)
}

export function isMinimalModeActive(sessionId: string): boolean {
  return states.get(sessionId) ?? false
}

export function resetMinimalMode(): void {
  states.clear()
}

export const minimalAgentMode: AgentMode = {
  name: 'minimal',
  description: 'Token-efficiency mode with core tools only',

  filterTools(tools: ToolDefinition[]): ToolDefinition[] {
    return tools.filter((t) => ALLOWED_TOOLS.has(t.name))
  },

  systemPrompt(base: string): string {
    return `${base}\n\nYou are in MINIMAL MODE. Keep responses concise and direct. Only core tools are available.`
  },
}

export function registerMinimalMode(api: ExtensionAPI): Disposable {
  return api.registerAgentMode(minimalAgentMode)
}
