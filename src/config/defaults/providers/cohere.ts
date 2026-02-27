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
  models: [
    // ── Command A (Latest) ───────────────────────────────
    {
      id: 'command-a',
      name: 'Command A',
      contextWindow: 128000,
      isDefault: true,
      pricing: { input: 2.5, output: 10 },
      capabilities: ['tools'],
    },
    {
      id: 'command-a-vision',
      name: 'Command A Vision',
      contextWindow: 128000,
      pricing: { input: 2.5, output: 10 },
      capabilities: ['vision', 'tools'],
    },
    // ── Command R Family ─────────────────────────────────
    {
      id: 'command-r-plus',
      name: 'Command R+',
      contextWindow: 128000,
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
    {
      id: 'command-r7b-12-2024',
      name: 'Command R 7B',
      contextWindow: 128000,
      pricing: { input: 0.0375, output: 0.15 },
      capabilities: ['tools'],
    },
  ],
}
