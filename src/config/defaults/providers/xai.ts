/**
 * xAI Provider — Grok models
 * https://docs.x.ai/docs
 * Last updated: Feb 2026
 */

import { XAILogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const xai: LLMProviderConfig = {
  id: 'xai',
  name: 'xAI',
  icon: XAILogo,
  description: 'Grok models for reasoning and code',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'grok-4-1-fast-reasoning',
  // Offline fallback — models.dev catalog provides the full list when online
  models: [
    {
      id: 'grok-4-1-fast-reasoning',
      name: 'Grok 4.1 Fast (Reasoning)',
      contextWindow: 2000000,
      isDefault: true,
      pricing: { input: 0.2, output: 0.5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'grok-code-fast-1',
      name: 'Grok Code Fast',
      contextWindow: 256000,
      pricing: { input: 0.2, output: 1.5 },
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'grok-4-0709',
      name: 'Grok 4',
      contextWindow: 256000,
      pricing: { input: 3, output: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
  ],
}
