/**
 * Gemini Provider
 */

import { GeminiLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const gemini: LLMProviderConfig = {
  id: 'gemini',
  name: 'Gemini',
  icon: GeminiLogo,
  description: 'Google Gemini models with large context',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'gemini-2.5-pro',
  models: [
    {
      id: 'gemini-2.5-pro',
      name: 'Gemini 2.5 Pro',
      contextWindow: 1048576,
      isDefault: true,
      pricing: { input: 1.25, output: 10 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gemini-2.5-flash',
      name: 'Gemini 2.5 Flash',
      contextWindow: 1048576,
      pricing: { input: 0.3, output: 2.5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gemini-3-flash-preview',
      name: 'Gemini 3 Flash',
      contextWindow: 1048576,
      pricing: { input: 0.5, output: 3 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
  ],
}
