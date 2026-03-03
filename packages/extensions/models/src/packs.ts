/**
 * Model packs — preset model configurations for the Praxis hierarchy.
 *
 * Each pack maps agent roles (commander, lead, worker) to a provider + model.
 * Used by the commander extension to configure per-tier model assignments.
 */

export interface ModelAssignment {
  provider: string
  model: string
}

export type ModelRole = 'summarizer' | 'committer' | 'namer' | 'verifier' | 'compactor'

export interface ModelPack {
  name: string
  description: string
  models: Record<string, ModelAssignment>
}

export const BUILTIN_PACKS: ModelPack[] = [
  {
    name: 'budget',
    description: 'Cost-effective models for routine tasks',
    models: {
      commander: { provider: 'anthropic', model: 'claude-haiku-4-5' },
      lead: { provider: 'anthropic', model: 'claude-sonnet-4' },
      worker: { provider: 'anthropic', model: 'claude-haiku-4-5' },
      summarizer: { provider: 'anthropic', model: 'claude-haiku-4-5' },
      committer: { provider: 'anthropic', model: 'claude-sonnet-4' },
      namer: { provider: 'anthropic', model: 'claude-haiku-4-5' },
      verifier: { provider: 'anthropic', model: 'claude-sonnet-4' },
      compactor: { provider: 'anthropic', model: 'claude-haiku-4-5' },
    },
  },
  {
    name: 'balanced',
    description: 'Good balance of capability and cost',
    models: {
      commander: { provider: 'anthropic', model: 'claude-sonnet-4' },
      lead: { provider: 'anthropic', model: 'claude-sonnet-4' },
      worker: { provider: 'anthropic', model: 'claude-haiku-4-5' },
      summarizer: { provider: 'anthropic', model: 'claude-sonnet-4' },
      committer: { provider: 'anthropic', model: 'claude-sonnet-4' },
      namer: { provider: 'anthropic', model: 'claude-haiku-4-5' },
      verifier: { provider: 'anthropic', model: 'claude-sonnet-4' },
      compactor: { provider: 'anthropic', model: 'claude-haiku-4-5' },
    },
  },
  {
    name: 'premium',
    description: 'Best models for complex tasks',
    models: {
      commander: { provider: 'anthropic', model: 'claude-opus-4' },
      lead: { provider: 'anthropic', model: 'claude-sonnet-4' },
      worker: { provider: 'anthropic', model: 'claude-sonnet-4' },
      summarizer: { provider: 'anthropic', model: 'claude-sonnet-4' },
      committer: { provider: 'anthropic', model: 'claude-opus-4' },
      namer: { provider: 'anthropic', model: 'claude-sonnet-4' },
      verifier: { provider: 'anthropic', model: 'claude-opus-4' },
      compactor: { provider: 'anthropic', model: 'claude-sonnet-4' },
    },
  },
]

export const MODEL_ROLES: ModelRole[] = [
  'summarizer',
  'committer',
  'namer',
  'verifier',
  'compactor',
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
export function resolveModelForTier(pack: ModelPack, tier: string): ModelAssignment | undefined {
  return pack.models[tier] ?? pack.models.worker
}

/** Resolve model assignment for named role. Falls back to praxis tier then worker. */
export function resolveModelForRole(
  pack: ModelPack,
  role: ModelRole,
  praxisTier = 'worker'
): ModelAssignment | undefined {
  return pack.models[role] ?? pack.models[praxisTier] ?? pack.models.worker
}

/** Resolve role-aware routing for praxis agents. Role takes precedence over tier. */
export function resolveModelForRouting(
  pack: ModelPack,
  routing: { tier?: string; role?: ModelRole }
): ModelAssignment | undefined {
  if (routing.role) return resolveModelForRole(pack, routing.role, routing.tier)
  if (routing.tier) return resolveModelForTier(pack, routing.tier)
  return pack.models.worker
}
