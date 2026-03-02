/**
 * Model packs — preset model configurations for the Praxis hierarchy.
 *
 * Each pack maps agent roles (commander, lead, worker) to a provider + model.
 * Used by the commander extension to configure per-tier model assignments.
 */

export interface ModelPack {
  name: string
  description: string
  models: Record<string, { provider: string; model: string }>
}

export const BUILTIN_PACKS: ModelPack[] = [
  {
    name: 'budget',
    description: 'Cost-effective models for routine tasks',
    models: {
      commander: { provider: 'anthropic', model: 'claude-haiku-4-5' },
      lead: { provider: 'anthropic', model: 'claude-sonnet-4' },
      worker: { provider: 'anthropic', model: 'claude-haiku-4-5' },
    },
  },
  {
    name: 'balanced',
    description: 'Good balance of capability and cost',
    models: {
      commander: { provider: 'anthropic', model: 'claude-sonnet-4' },
      lead: { provider: 'anthropic', model: 'claude-sonnet-4' },
      worker: { provider: 'anthropic', model: 'claude-haiku-4-5' },
    },
  },
  {
    name: 'premium',
    description: 'Best models for complex tasks',
    models: {
      commander: { provider: 'anthropic', model: 'claude-opus-4' },
      lead: { provider: 'anthropic', model: 'claude-sonnet-4' },
      worker: { provider: 'anthropic', model: 'claude-sonnet-4' },
    },
  },
]

/** Get a model pack by name. Returns undefined if not found. */
export function getModelPack(name: string): ModelPack | undefined {
  return BUILTIN_PACKS.find((p) => p.name === name)
}

/** List all available model pack names. */
export function listModelPacks(): string[] {
  return BUILTIN_PACKS.map((p) => p.name)
}

/**
 * Resolve the model assignment for a given agent tier from a pack.
 * Falls back to 'worker' if the tier key is not found.
 */
export function resolveModelForTier(
  pack: ModelPack,
  tier: string
): { provider: string; model: string } | undefined {
  return pack.models[tier] ?? pack.models.worker
}
