import { getPlatform } from '@ava/core-v2/platform'
import type { AgentRole } from './types.js'

export interface PraxisModelConfig {
  director: { provider: string; model: string }
  'tech-lead': { provider: string; model: string }
  engineer: { provider: string; model: string }
  reviewer: { provider: string; model: string }
  subagent: { provider: string; model: string }
}

const DEFAULT_CONFIG: PraxisModelConfig = {
  director: { provider: 'openrouter', model: 'anthropic/claude-opus-4-6' },
  'tech-lead': { provider: 'openrouter', model: 'anthropic/claude-sonnet-4-6' },
  engineer: { provider: 'openrouter', model: 'anthropic/claude-haiku-4-5' },
  reviewer: { provider: 'openrouter', model: 'anthropic/claude-sonnet-4-6' },
  subagent: { provider: 'openrouter', model: 'anthropic/claude-haiku-4-5' },
}

let cachedConfig: PraxisModelConfig | null = null

function mergeConfig(
  base: PraxisModelConfig,
  override: Partial<PraxisModelConfig> | undefined
): PraxisModelConfig {
  if (!override) return base
  return {
    director: { ...base.director, ...override.director },
    'tech-lead': { ...base['tech-lead'], ...override['tech-lead'] },
    engineer: { ...base.engineer, ...override.engineer },
    reviewer: { ...base.reviewer, ...override.reviewer },
    subagent: { ...base.subagent, ...override.subagent },
  }
}

async function readConfigAt(path: string): Promise<Partial<PraxisModelConfig> | undefined> {
  const fs = getPlatform().fs
  if (!(await fs.exists(path))) return undefined
  const raw = await fs.readFile(path)
  const parsed = JSON.parse(raw) as { praxis?: { models?: Partial<PraxisModelConfig> } }
  return parsed.praxis?.models
}

export async function loadModelConfig(): Promise<PraxisModelConfig> {
  if (cachedConfig) return cachedConfig

  const global = await readConfigAt('~/.ava/config.json').catch(() => undefined)
  const project = await readConfigAt('.ava/config.json').catch(() => undefined)

  cachedConfig = mergeConfig(mergeConfig(DEFAULT_CONFIG, global), project)
  return cachedConfig
}

export async function getModelForRole(
  role: AgentRole
): Promise<{ provider: string; model: string }> {
  const cfg = await loadModelConfig()
  if (role === 'subagent') return cfg.subagent
  return cfg[role]
}

export function resetModelConfigCache(): void {
  cachedConfig = null
}

export function getDefaultModelConfig(): PraxisModelConfig {
  return DEFAULT_CONFIG
}
