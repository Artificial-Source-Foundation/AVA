import { getFallbackModel, isModelAvailable } from '../../models/src/availability.js'
import type { PraxisModelConfig } from './model-config.js'
import type { AgentRole } from './types.js'

export const FALLBACK_CHAINS: Record<AgentRole, string[]> = {
  director: ['claude-opus-4-6', 'claude-sonnet-4-6', 'claude-haiku-4-5'],
  'tech-lead': ['claude-sonnet-4-6', 'claude-haiku-4-5'],
  engineer: ['claude-haiku-4-5', 'gpt-4o-mini'],
  reviewer: ['claude-sonnet-4-6', 'claude-haiku-4-5'],
  subagent: ['claude-haiku-4-5', 'gpt-4o-mini'],
}

export interface ResolvedModel {
  role: AgentRole
  provider: string
  model: string
  fallbackUsed: boolean
}

export function resolveModel(role: AgentRole, config: PraxisModelConfig): ResolvedModel {
  const configured = config[role]
  if (isModelAvailable(configured.provider, configured.model)) {
    return {
      role,
      provider: configured.provider,
      model: configured.model,
      fallbackUsed: false,
    }
  }

  const fallback = getFallbackModel(configured.provider, configured.model)
  if (fallback) {
    return {
      role,
      provider: fallback.provider,
      model: fallback.model,
      fallbackUsed: true,
    }
  }

  for (const candidate of FALLBACK_CHAINS[role]) {
    if (isModelAvailable(configured.provider, candidate)) {
      return {
        role,
        provider: configured.provider,
        model: candidate,
        fallbackUsed: true,
      }
    }
  }

  throw new Error(`No available model for role '${role}'. Tried configured and fallback chain.`)
}
