/**
 * Model Fetcher — Static / Documented Provider Models
 *
 * Provider model lists that are hardcoded rather than fetched from APIs.
 * Includes: Anthropic, Alibaba Cloud, and OpenAI-compatible endpoint configs.
 *
 * Anthropic and Alibaba models are derived from the canonical provider defaults
 * in src/config/defaults/providers/ to avoid duplication.
 */

import type { ProviderModel } from '../../config/defaults/provider-defaults'
import { alibaba } from '../../config/defaults/providers/alibaba'
import { anthropic } from '../../config/defaults/providers/anthropic'
import type { FetchedModel, OpenAICompatConfig } from './model-fetcher-types'

/** Convert a ProviderModel to FetchedModel format */
function toFetchedModel(m: ProviderModel): FetchedModel {
  return {
    id: m.id,
    name: m.name,
    contextWindow: m.contextWindow,
    pricing:
      m.pricing?.input != null && m.pricing?.output != null
        ? { prompt: m.pricing.input, completion: m.pricing.output }
        : undefined,
    capabilities: m.capabilities,
  }
}

// ============================================================================
// Anthropic (derived from provider defaults)
// ============================================================================

export function getAnthropicModels(): FetchedModel[] {
  return anthropic.models.map(toFetchedModel)
}

// ============================================================================
// Alibaba Cloud (derived from provider defaults)
// ============================================================================

export function getAlibabaModels(): FetchedModel[] {
  return alibaba.models.map(toFetchedModel)
}

// ============================================================================
// OpenAI-Compatible Endpoint Configs (xAI, Mistral, Groq, DeepSeek, Together, Kimi)
// ============================================================================

export const OPENAI_COMPAT_CONFIGS: Record<string, OpenAICompatConfig> = {
  xai: {
    baseUrl: 'https://api.x.ai/v1/models',
    providerName: 'xAI',
    filterFn: (m) => m.owned_by.includes('xai') || m.id.startsWith('grok'),
  },
  mistral: {
    baseUrl: 'https://api.mistral.ai/v1/models',
    providerName: 'Mistral',
  },
  groq: {
    baseUrl: 'https://api.groq.com/openai/v1/models',
    providerName: 'Groq',
    filterFn: (m) => !m.id.includes('whisper') && !m.id.includes('tool-use'),
  },
  deepseek: {
    baseUrl: 'https://api.deepseek.com/models',
    providerName: 'DeepSeek',
  },
  together: {
    baseUrl: 'https://api.together.xyz/v1/models',
    providerName: 'Together',
    filterFn: (m) => m.id.includes('Instruct') || m.id.includes('chat') || m.id.includes('Chat'),
  },
  kimi: {
    baseUrl: 'https://api.moonshot.cn/v1/models',
    providerName: 'Kimi',
  },
}
