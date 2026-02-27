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
  models: [
    // ── Flagship ──────────────────────────────────────────
    {
      id: 'mistral-large-latest',
      name: 'Mistral Large 3',
      contextWindow: 256000,
      isDefault: true,
      pricing: { input: 0.5, output: 1.5 },
      capabilities: ['vision', 'tools'],
    },
    {
      id: 'mistral-medium-latest',
      name: 'Mistral Medium 3.1',
      contextWindow: 128000,
      pricing: { input: 0.4, output: 2 },
      capabilities: ['vision', 'tools'],
    },
    {
      id: 'mistral-small-latest',
      name: 'Mistral Small 3.2',
      contextWindow: 128000,
      pricing: { input: 0.1, output: 0.3 },
      capabilities: ['vision', 'tools'],
    },
    // ── Reasoning (Magistral) ─────────────────────────────
    {
      id: 'magistral-medium-latest',
      name: 'Magistral Medium 1.2',
      contextWindow: 128000,
      pricing: { input: 2, output: 5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'magistral-small-latest',
      name: 'Magistral Small 1.2',
      contextWindow: 128000,
      pricing: { input: 0.5, output: 1.5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    // ── Code ──────────────────────────────────────────────
    {
      id: 'devstral-latest',
      name: 'Devstral 2',
      contextWindow: 256000,
      pricing: { input: 0.4, output: 2 },
      capabilities: ['tools'],
    },
    {
      id: 'codestral-latest',
      name: 'Codestral',
      contextWindow: 128000,
      pricing: { input: 0.3, output: 0.9 },
      capabilities: ['tools'],
    },
  ],
}
