/**
 * DeepSeek Provider — Open-weight reasoning models (V3.2)
 * https://api-docs.deepseek.com/quick_start/pricing
 * Last updated: Feb 2026
 */

import { DeepSeekLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const deepseek: LLMProviderConfig = {
  id: 'deepseek',
  name: 'DeepSeek',
  icon: DeepSeekLogo,
  description: 'Open-weight reasoning and coding models',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'deepseek-chat',
  models: [
    {
      id: 'deepseek-chat',
      name: 'DeepSeek V3.2',
      contextWindow: 128000,
      isDefault: true,
      pricing: { input: 0.28, output: 0.42 },
      capabilities: ['tools'],
    },
    {
      id: 'deepseek-reasoner',
      name: 'DeepSeek V3.2 Reasoner',
      contextWindow: 128000,
      pricing: { input: 0.28, output: 0.42 },
      capabilities: ['tools', 'reasoning'],
    },
  ],
}
