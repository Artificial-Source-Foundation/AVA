/**
 * Google Provider — Gemini models
 * https://ai.google.dev/gemini-api/docs/models
 * Last updated: Feb 2026
 */

import { GoogleLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const google: LLMProviderConfig = {
  id: 'google',
  name: 'Google',
  icon: GoogleLogo,
  description: 'Gemini models with large context',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'gemini-2.5-pro',
  // Offline fallback — models.dev catalog provides the full list when online
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
