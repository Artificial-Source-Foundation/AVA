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
  // Offline fallback — models.dev catalog provides the full list when online
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
      id: 'gpt-5-mini',
      name: 'GPT-5 Mini',
      contextWindow: 400000,
      pricing: { input: 0.25, output: 2 },
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
      contextWindow: 1048576,
      pricing: { input: 0.4, output: 1.6 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.3-codex',
      name: 'GPT-5.3 Codex',
      contextWindow: 400000,
      pricing: { input: 1.75, output: 14 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'o4-mini',
      name: 'o4 Mini',
      contextWindow: 200000,
      pricing: { input: 1.1, output: 4.4 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-4.1',
      name: 'GPT-4.1',
      contextWindow: 1047576,
      pricing: { input: 2, output: 8 },
      capabilities: ['vision', 'tools'],
    },
  ],
}
