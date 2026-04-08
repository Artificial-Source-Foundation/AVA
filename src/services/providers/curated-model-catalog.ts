/**
 * Repo-owned model catalog bridge.
 *
 * Historical note: this module previously fetched from models.dev. It now
 * hydrates from the backend `list_models` command so AVA uses repo-owned model
 * metadata only.
 */

import type { ProviderModel } from '../../config/defaults/provider-defaults'
import { type AnyLLMProvider, normalizeProviderId } from '../../types/llm'
import type { ModelInfo } from '../../types/rust-ipc'
import { rustBackend } from '../rust-bridge'

export interface ModelsDevModel {
  id: string
  name: string
  attachment?: boolean
  reasoning?: boolean
  tool_call?: boolean
  modalities?: {
    input?: string[]
    output?: string[]
  }
  cost?: {
    input?: number
    output?: number
  }
  limit?: {
    context?: number
    output?: number
  }
}

type ModelsDevCatalog = Record<string, Record<string, ModelsDevModel>>

const CURATED_PROVIDER_MODEL_IDS: Partial<Record<AnyLLMProvider, string[]>> = {
  openai: [
    'gpt-5-mini',
    'gpt-5.2',
    'gpt-5.2-pro',
    'gpt-5.2-codex',
    'gpt-5.3',
    'gpt-5.3-codex',
    'gpt-5.3-codex-spark',
    'gpt-5.4',
    'gpt-5.4-mini',
    'gpt-5.4-nano',
  ],
}

const BLOCKED_MODEL_PATTERNS = ['aurora', 'codestral-2501']

let memoryCatalog: ModelsDevCatalog | null = null

function isNonCodingModel(model: ModelsDevModel): boolean {
  const id = model.id.toLowerCase()
  if (BLOCKED_MODEL_PATTERNS.some((pattern) => id.includes(pattern))) return true
  if (model.tool_call === false) return true
  return false
}

function transformModel(model: ModelsDevModel): ProviderModel {
  const capabilities: string[] = []
  if (model.tool_call) capabilities.push('tools')
  if (model.reasoning) capabilities.push('reasoning')
  if (model.attachment) capabilities.push('vision')
  if (model.cost?.input === 0 && model.cost?.output === 0) capabilities.push('free')

  const pricing =
    model.cost?.input !== undefined && model.cost?.output !== undefined
      ? { input: model.cost.input, output: model.cost.output }
      : undefined

  return {
    id: model.id,
    name: model.name || model.id,
    contextWindow: model.limit?.context ?? 4096,
    ...(pricing && { pricing }),
    ...(capabilities.length > 0 && { capabilities }),
  }
}

function backendModelToCatalogEntry(model: ModelInfo): ModelsDevModel {
  return {
    id: model.id,
    name: model.name,
    attachment: model.vision,
    reasoning: model.reasoning,
    tool_call: model.toolCall,
    cost: {
      input: model.costInput,
      output: model.costOutput,
    },
    limit: {
      context: model.contextWindow,
      output: model.maxOutput ?? undefined,
    },
  }
}

function isCuratedProviderModel(provider: string, modelId: string): boolean {
  const canonicalProvider = normalizeProviderId(provider)
  const curatedIds = CURATED_PROVIDER_MODEL_IDS[canonicalProvider as AnyLLMProvider]
  if (!curatedIds) return true
  return curatedIds.includes(modelId)
}

export async function syncModelsCatalog(): Promise<ModelsDevCatalog | null> {
  try {
    const models = await rustBackend.listModels()
    const catalog: ModelsDevCatalog = {}

    for (const model of models) {
      if (!isCuratedProviderModel(model.provider as string, model.id)) continue
      const providerId = normalizeProviderId(model.provider)
      const providerBucket = catalog[providerId] ?? {}
      catalog[providerId] = providerBucket
      providerBucket[model.id] = backendModelToCatalogEntry(model)
    }

    memoryCatalog = catalog
    return catalog
  } catch {
    return memoryCatalog
  }
}

export function getModelsDevModels(avaProviderId: AnyLLMProvider): ProviderModel[] {
  if (!memoryCatalog) return []

  const provider = memoryCatalog[normalizeProviderId(avaProviderId)]
  if (!provider) return []

  return Object.values(provider)
    .filter((m) => !isNonCodingModel(m))
    .map(transformModel)
}

export function getModelFromCatalog(
  modelId: string,
  avaProviderId?: AnyLLMProvider
): ModelsDevModel | null {
  if (!memoryCatalog) return null

  if (avaProviderId) {
    return memoryCatalog[normalizeProviderId(avaProviderId)]?.[modelId] ?? null
  }

  for (const provider of Object.values(memoryCatalog)) {
    if (provider[modelId]) return provider[modelId]
  }
  return null
}

export function isBlockedModelId(modelId: string): boolean {
  const id = modelId.toLowerCase()
  return BLOCKED_MODEL_PATTERNS.some((pattern) => id.includes(pattern))
}

export function _resetCatalogCache(): void {
  memoryCatalog = null
}
