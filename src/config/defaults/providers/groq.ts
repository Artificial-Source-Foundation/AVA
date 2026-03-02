/**
 * Groq Provider — Ultra-fast inference
 * https://console.groq.com/docs/models
 * Last updated: Feb 2026
 */

import { GroqLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const groq: LLMProviderConfig = {
  id: 'groq',
  name: 'Groq',
  icon: GroqLogo,
  description: 'Ultra-fast inference on open models',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'meta-llama/llama-4-maverick-17b-128e-instruct',
  // Offline fallback — models.dev catalog provides the full list when online
  models: [
    {
      id: 'meta-llama/llama-4-maverick-17b-128e-instruct',
      name: 'Llama 4 Maverick',
      contextWindow: 128000,
      isDefault: true,
      capabilities: ['vision', 'tools', 'free'],
    },
    {
      id: 'llama-3.3-70b-versatile',
      name: 'Llama 3.3 70B',
      contextWindow: 128000,
      capabilities: ['tools', 'free'],
    },
    {
      id: 'qwen/qwen3-32b',
      name: 'Qwen 3 32B',
      contextWindow: 128000,
      capabilities: ['tools', 'reasoning', 'free'],
    },
  ],
}
