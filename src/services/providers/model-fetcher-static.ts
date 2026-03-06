/**
 * Model Fetcher — Static / Documented Provider Models
 *
 * Provider model lists that are hardcoded rather than fetched from APIs.
 * Includes: Anthropic, Alibaba Cloud, and OpenAI-compatible endpoint configs.
 */

import type { FetchedModel, OpenAICompatConfig } from './model-fetcher-types'

// ============================================================================
// Anthropic (documented, no API)
// ============================================================================

export function getAnthropicModels(): FetchedModel[] {
  return [
    {
      id: 'claude-opus-4-6',
      name: 'Claude Opus 4.6',
      contextWindow: 200000,
      pricing: { prompt: 5, completion: 25 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-sonnet-4-6',
      name: 'Claude Sonnet 4.6',
      contextWindow: 200000,
      pricing: { prompt: 3, completion: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-haiku-4-5-20251001',
      name: 'Claude Haiku 4.5',
      contextWindow: 200000,
      pricing: { prompt: 1, completion: 5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-sonnet-4-5-20250929',
      name: 'Claude Sonnet 4.5',
      contextWindow: 200000,
      pricing: { prompt: 3, completion: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-opus-4-5-20251101',
      name: 'Claude Opus 4.5',
      contextWindow: 200000,
      pricing: { prompt: 5, completion: 25 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-opus-4-1-20250805',
      name: 'Claude Opus 4.1',
      contextWindow: 200000,
      pricing: { prompt: 15, completion: 75 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-sonnet-4-20250514',
      name: 'Claude Sonnet 4',
      contextWindow: 200000,
      pricing: { prompt: 3, completion: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-opus-4-20250514',
      name: 'Claude Opus 4',
      contextWindow: 200000,
      pricing: { prompt: 15, completion: 75 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
  ]
}

// ============================================================================
// Alibaba Cloud (documented, no API)
// ============================================================================

export function getAlibabaModels(): FetchedModel[] {
  return [
    {
      id: 'qwen3.5-plus',
      name: 'Qwen3.5 Plus',
      contextWindow: 1000000,
      capabilities: ['tools', 'reasoning', 'vision'],
    },
    {
      id: 'qwen3-max-2026-01-23',
      name: 'Qwen3 Max',
      contextWindow: 1000000,
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'qwen3-coder-next',
      name: 'Qwen3 Coder Next',
      contextWindow: 262144,
      capabilities: ['tools'],
    },
    {
      id: 'qwen3-coder-plus',
      name: 'Qwen3 Coder Plus',
      contextWindow: 1000000,
      capabilities: ['tools'],
    },
    { id: 'glm-5', name: 'GLM-5', contextWindow: 128000, capabilities: ['tools', 'reasoning'] },
    { id: 'glm-4.7', name: 'GLM-4.7', contextWindow: 128000, capabilities: ['tools', 'reasoning'] },
    {
      id: 'kimi-k2.5',
      name: 'Kimi K2.5',
      contextWindow: 131072,
      capabilities: ['tools', 'reasoning', 'vision'],
    },
    {
      id: 'MiniMax-M2.5',
      name: 'MiniMax M2.5',
      contextWindow: 1000000,
      capabilities: ['tools', 'reasoning'],
    },
  ]
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
