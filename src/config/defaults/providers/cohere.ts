/**
 * Cohere Provider — Enterprise RAG and command models
 * https://docs.cohere.com/docs/models
 * Last updated: Feb 2026
 */

import { CohereLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const cohere: LLMProviderConfig = {
  id: 'cohere',
  name: 'Cohere',
  icon: CohereLogo,
  description: 'Enterprise RAG and command models',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'command-a',
  // Offline fallback — models.dev catalog provides the full list when online
  models: [
    {
      id: 'command-a',
      name: 'Command A',
      contextWindow: 128000,
      isDefault: true,
      pricing: { input: 2.5, output: 10 },
      capabilities: ['tools'],
    },
    {
      id: 'command-r',
      name: 'Command R',
      contextWindow: 128000,
      pricing: { input: 0.15, output: 0.6 },
      capabilities: ['tools'],
    },
  ],
}
