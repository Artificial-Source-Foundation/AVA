/**
 * OpenAI Provider — GPT, o-series, and Codex models
 * https://platform.openai.com/docs/models
 * Last updated: Feb 2026
 */

import { OpenAILogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const openai: LLMProviderConfig = {
  id: 'openai',
  name: 'OpenAI',
  icon: OpenAILogo,
  description: 'GPT and reasoning models',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'gpt-5.2',
  // Offline fallback — the curated backend catalog provides the fuller list at runtime
  models: [
    {
      id: 'gpt-5.2',
      name: 'GPT-5.2',
      contextWindow: 400000,
      isDefault: true,
      pricing: { input: 1.75, output: 14 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.2-pro',
      name: 'GPT-5.2 Pro',
      contextWindow: 200000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.2-codex',
      name: 'GPT-5.2 Codex',
      contextWindow: 200000,
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'gpt-5-mini',
      name: 'GPT-5 Mini',
      contextWindow: 400000,
      pricing: { input: 0.25, output: 2 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.3',
      name: 'GPT-5.3',
      contextWindow: 200000,
      pricing: { input: 1.75, output: 14 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.4',
      name: 'GPT-5.4',
      contextWindow: 1048576,
      pricing: { input: 2.5, output: 10 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.4-mini',
      name: 'GPT-5.4 Mini',
      contextWindow: 400000,
      pricing: { input: 0.75, output: 4.5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.4-nano',
      name: 'GPT-5.4 Nano',
      contextWindow: 400000,
      pricing: { input: 0.2, output: 1.25 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.3-codex',
      name: 'GPT-5.3 Codex',
      contextWindow: 200000,
      pricing: { input: 1.75, output: 14 },
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'gpt-5.3-codex-spark',
      name: 'GPT-5.3 Codex Spark',
      contextWindow: 200000,
      capabilities: ['tools', 'reasoning'],
    },
  ],
}
