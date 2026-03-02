/**
 * OpenRouter Provider — Unified access to 300+ models
 * https://openrouter.ai/docs
 * Last updated: Feb 2026 — only models with tool/function calling support
 */

import { OpenRouterLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const openrouter: LLMProviderConfig = {
  id: 'openrouter',
  name: 'OpenRouter',
  icon: OpenRouterLogo,
  description: 'Access 300+ models via single API',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'anthropic/claude-sonnet-4.6',
  // Offline fallback — OpenRouter API provides 300+ models when online
  models: [
    {
      id: 'anthropic/claude-sonnet-4.6',
      name: 'Claude Sonnet 4.6',
      contextWindow: 1000000,
      isDefault: true,
      pricing: { input: 3, output: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'openai/gpt-5.3-codex',
      name: 'GPT-5.3 Codex',
      contextWindow: 400000,
      pricing: { input: 1.75, output: 14 },
      capabilities: ['vision', 'tools', 'reasoning', 'thinking'],
    },
    {
      id: 'google/gemini-2.5-pro',
      name: 'Gemini 2.5 Pro',
      contextWindow: 1048576,
      pricing: { input: 1.25, output: 10 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'deepseek/deepseek-v3.2',
      name: 'DeepSeek V3.2',
      contextWindow: 163840,
      pricing: { input: 0.25, output: 0.4 },
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'anthropic/claude-haiku-4.5',
      name: 'Claude Haiku 4.5',
      contextWindow: 200000,
      pricing: { input: 1, output: 5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
  ],
}
