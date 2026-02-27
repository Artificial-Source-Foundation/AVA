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
  models: [
    // ── Llama 4 ──────────────────────────────────────────
    {
      id: 'meta-llama/llama-4-maverick-17b-128e-instruct',
      name: 'Llama 4 Maverick',
      contextWindow: 128000,
      isDefault: true,
      pricing: { input: 0.5, output: 0.77 },
      capabilities: ['vision', 'tools', 'free'],
    },
    {
      id: 'meta-llama/llama-4-scout-17b-16e-instruct',
      name: 'Llama 4 Scout',
      contextWindow: 128000,
      pricing: { input: 0.11, output: 0.34 },
      capabilities: ['vision', 'tools', 'free'],
    },
    // ── Llama 3.3 ────────────────────────────────────────
    {
      id: 'llama-3.3-70b-versatile',
      name: 'Llama 3.3 70B',
      contextWindow: 128000,
      capabilities: ['tools', 'free'],
    },
    {
      id: 'llama-3.1-8b-instant',
      name: 'Llama 3.1 8B',
      contextWindow: 128000,
      capabilities: ['tools', 'free'],
    },
    // ── Other Open Models ────────────────────────────────
    {
      id: 'qwen/qwen3-32b',
      name: 'Qwen 3 32B',
      contextWindow: 128000,
      capabilities: ['tools', 'reasoning', 'free'],
    },
    {
      id: 'moonshotai/kimi-k2-instruct-0905',
      name: 'Kimi K2',
      contextWindow: 128000,
      capabilities: ['tools', 'free'],
    },
  ],
}
