/**
 * Mistral Provider — European AI models
 * https://docs.mistral.ai/getting-started/models/
 * Last updated: Feb 2026
 */

import { MistralLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const mistral: LLMProviderConfig = {
  id: 'mistral',
  name: 'Mistral',
  icon: MistralLogo,
  description: 'European AI with code-specialized models',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'mistral-large-latest',
  // Offline fallback — models.dev catalog provides the full list when online
  models: [
    {
      id: 'mistral-large-latest',
      name: 'Mistral Large 3',
      contextWindow: 256000,
      isDefault: true,
      pricing: { input: 0.5, output: 1.5 },
      capabilities: ['vision', 'tools'],
    },
    {
      id: 'devstral-latest',
      name: 'Devstral 2',
      contextWindow: 256000,
      pricing: { input: 0.4, output: 2 },
      capabilities: ['tools'],
    },
    {
      id: 'magistral-medium-latest',
      name: 'Magistral Medium 1.2',
      contextWindow: 128000,
      pricing: { input: 2, output: 5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
  ],
}
