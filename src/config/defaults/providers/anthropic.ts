/**
 * Anthropic Provider — Claude models
 * https://docs.anthropic.com/en/docs/about-claude/models
 * Last updated: Feb 2026
 */

import { AnthropicLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const anthropic: LLMProviderConfig = {
  id: 'anthropic',
  name: 'Anthropic',
  icon: AnthropicLogo,
  description: 'Claude models with advanced reasoning',
  enabled: true,
  status: 'disconnected',
  defaultModel: 'claude-sonnet-4-6',
  models: [
    // ── Latest (Current Generation) ────────────────────────
    {
      id: 'claude-opus-4-6',
      name: 'Claude Opus 4.6',
      contextWindow: 200000,
      pricing: { input: 5, output: 25 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-sonnet-4-6',
      name: 'Claude Sonnet 4.6',
      contextWindow: 200000,
      isDefault: true,
      pricing: { input: 3, output: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-haiku-4-5-20251001',
      name: 'Claude Haiku 4.5',
      contextWindow: 200000,
      pricing: { input: 1, output: 5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    // ── Legacy (still available) ───────────────────────────
    {
      id: 'claude-sonnet-4-5-20250929',
      name: 'Claude Sonnet 4.5',
      contextWindow: 200000,
      pricing: { input: 3, output: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-opus-4-5-20251101',
      name: 'Claude Opus 4.5',
      contextWindow: 200000,
      pricing: { input: 5, output: 25 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-opus-4-1-20250805',
      name: 'Claude Opus 4.1',
      contextWindow: 200000,
      pricing: { input: 15, output: 75 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-sonnet-4-20250514',
      name: 'Claude Sonnet 4',
      contextWindow: 200000,
      pricing: { input: 3, output: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-opus-4-20250514',
      name: 'Claude Opus 4',
      contextWindow: 200000,
      pricing: { input: 15, output: 75 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
  ],
}
