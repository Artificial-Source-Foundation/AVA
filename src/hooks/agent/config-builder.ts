/**
 * Agent Config Builder
 *
 * Builds the AgentConfig from current app settings, resolving
 * system prompt, allowed tools, provider, and thinking options.
 */

import type { AgentConfig } from '@ava/core-v2/agent'
import { getAgentModes } from '@ava/core-v2/extensions'
import { resolveProvider } from '../../services/llm/bridge'
import type { CompletionNotificationSettings } from '../../services/notifications'
import { buildSystemPromptAfterInstructions } from '../chat/prompt-builder'
import { SOLO_EXCLUDED } from './tool-execution'

// ============================================================================
// Types
// ============================================================================

/** Dependencies injected from the store layer */
export interface ConfigDeps {
  currentProjectDir: () => string | undefined
  settingsRef: {
    settings: () => {
      agentLimits: { agentMaxTurns: number; agentMaxTimeMinutes: number }
      generation: {
        delegationEnabled: boolean
        reasoningEffort: string
        customInstructions?: string
      }
      behavior: { sessionAutoTitle: boolean }
      permissionMode: string
      notifications: CompletionNotificationSettings
    }
  }
}

// ============================================================================
// Builder
// ============================================================================

/**
 * Build agent configuration from current settings + optional overrides.
 * Resolves system prompt, allowed tools, and provider for the model.
 */
export async function buildAgentConfig(
  model: string,
  deps: ConfigDeps,
  overrides?: Partial<AgentConfig>
): Promise<Partial<AgentConfig>> {
  const limits = deps.settingsRef.settings().agentLimits
  const generation = deps.settingsRef.settings().generation
  const delegationEnabled = generation.delegationEnabled
  const reasoningEffort = generation.reasoningEffort
  const thinking =
    reasoningEffort !== 'off'
      ? {
          enabled: true,
          effort: reasoningEffort as
            | 'none'
            | 'minimal'
            | 'low'
            | 'medium'
            | 'high'
            | 'xhigh'
            | 'max',
        }
      : undefined

  const { getToolDefinitions } = await import('@ava/core-v2/tools')
  const allToolNames = getToolDefinitions().map((t) => t.name)
  const allowedTools = delegationEnabled
    ? undefined
    : allToolNames.filter((n) => !n.startsWith('delegate_') && !SOLO_EXCLUDED.has(n))

  const cwd = deps.currentProjectDir() || '.'
  const customInstructions = generation.customInstructions
  const systemPrompt = await buildSystemPromptAfterInstructions(model, cwd, customInstructions)

  const provider = resolveProvider(model)
  const toolChoice = 'auto' as const

  return {
    provider,
    model,
    systemPrompt,
    maxTurns: limits.agentMaxTurns,
    maxTimeMinutes: limits.agentMaxTimeMinutes,
    toolChoiceStrategy: toolChoice,
    allowedTools,
    toolMode:
      delegationEnabled && getAgentModes().has('praxis')
        ? 'praxis'
        : delegationEnabled && getAgentModes().has('team')
          ? 'team'
          : undefined,
    thinking,
    ...overrides,
  }
}
