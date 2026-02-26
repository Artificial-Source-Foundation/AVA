/**
 * Commander extension — team hierarchy with auto-routing.
 *
 * Registers the 'team' agent mode with worker delegation.
 */

import type { AgentMode, Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { ToolDefinition } from '@ava/core-v2/llm'
import { BUILTIN_WORKERS } from './workers.js'

const teamAgentMode: AgentMode = {
  name: 'team',
  description: 'Team Lead mode: delegates to specialized workers',

  filterTools(tools: ToolDefinition[]): ToolDefinition[] {
    // Team Lead gets all tools plus delegation instructions
    return tools
  },

  systemPrompt(base: string): string {
    const workerList = BUILTIN_WORKERS.map((w) => `- ${w.displayName}: ${w.description}`).join('\n')

    return `${base}\n\nYou are the Team Lead. You can delegate tasks to specialized workers:\n${workerList}\n\nAnalyze each task and decide whether to handle it yourself or delegate to the most appropriate worker.`
  },
}

export function activate(api: ExtensionAPI): Disposable {
  const disposable = api.registerAgentMode(teamAgentMode)
  api.log.debug(`Registered team mode with ${BUILTIN_WORKERS.length} built-in workers`)

  return disposable
}
