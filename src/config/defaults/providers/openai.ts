/**
 * OpenAI Provider — GPT, o-series, and Codex models
 * https://platform.openai.com/docs/models
 * Last updated: Feb 2026
 */

import { OpenAILogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const openai: LLMProviderConfig = {
  id: 'openai',
  name: 'OpenAI',
  icon: OpenAILogo,
  description: 'GPT and reasoning models',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'gpt-5.2',
  models: [
    // ── GPT-5.2 (Flagship) ────────────────────────────────
    {
      id: 'gpt-5.2',
      name: 'GPT-5.2',
      contextWindow: 400000,
      isDefault: true,
      pricing: { input: 1.75, output: 14 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    // ── GPT-5.1 ───────────────────────────────────────────
    {
      id: 'gpt-5.1',
      name: 'GPT-5.1',
      contextWindow: 400000,
      pricing: { input: 1.25, output: 10 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    // ── GPT-5 Family ──────────────────────────────────────
    {
      id: 'gpt-5',
      name: 'GPT-5',
      contextWindow: 400000,
      pricing: { input: 1.25, output: 10 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5-mini',
      name: 'GPT-5 Mini',
      contextWindow: 400000,
      pricing: { input: 0.25, output: 2 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5-nano',
      name: 'GPT-5 Nano',
      contextWindow: 400000,
      pricing: { input: 0.05, output: 0.4 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    // ── Codex (Agentic Coding) ────────────────────────────
    {
      id: 'gpt-5.3-codex',
      name: 'GPT-5.3 Codex',
      contextWindow: 400000,
      pricing: { input: 1.75, output: 14 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.2-codex',
      name: 'GPT-5.2 Codex',
      contextWindow: 400000,
      pricing: { input: 1.75, output: 14 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.1-codex',
      name: 'GPT-5.1 Codex',
      contextWindow: 400000,
      pricing: { input: 1.25, output: 10 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.1-codex-mini',
      name: 'GPT-5.1 Codex Mini',
      contextWindow: 400000,
      pricing: { input: 0.25, output: 2 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'codex-mini-latest',
      name: 'Codex Mini',
      contextWindow: 200000,
      pricing: { input: 1.5, output: 6 },
      capabilities: ['tools', 'reasoning'],
    },
    // ── GPT-4.1 (1M Context) ──────────────────────────────
    {
      id: 'gpt-4.1',
      name: 'GPT-4.1',
      contextWindow: 1047576,
      pricing: { input: 2, output: 8 },
      capabilities: ['vision', 'tools'],
    },
    {
      id: 'gpt-4.1-mini',
      name: 'GPT-4.1 Mini',
      contextWindow: 1047576,
      pricing: { input: 0.4, output: 1.6 },
      capabilities: ['vision', 'tools'],
    },
    {
      id: 'gpt-4.1-nano',
      name: 'GPT-4.1 Nano',
      contextWindow: 1047576,
      pricing: { input: 0.1, output: 0.4 },
      capabilities: ['vision', 'tools'],
    },
    // ── o-series (Reasoning) ──────────────────────────────
    {
      id: 'o3',
      name: 'o3',
      contextWindow: 200000,
      pricing: { input: 2, output: 8 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'o4-mini',
      name: 'o4 Mini',
      contextWindow: 200000,
      pricing: { input: 1.1, output: 4.4 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'o3-mini',
      name: 'o3 Mini',
      contextWindow: 200000,
      pricing: { input: 1.1, output: 4.4 },
      capabilities: ['tools', 'reasoning'],
    },
    // ── Legacy ────────────────────────────────────────────
    {
      id: 'gpt-4o',
      name: 'GPT-4o',
      contextWindow: 128000,
      pricing: { input: 2.5, output: 10 },
      capabilities: ['vision', 'tools'],
    },
    {
      id: 'gpt-4o-mini',
      name: 'GPT-4o Mini',
      contextWindow: 128000,
      pricing: { input: 0.15, output: 0.6 },
      capabilities: ['vision', 'tools'],
    },
  ],
}
