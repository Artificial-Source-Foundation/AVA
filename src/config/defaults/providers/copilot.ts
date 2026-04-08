/**
 * GitHub Copilot Provider — Multi-model via device code auth
 * https://docs.github.com/en/copilot
 */

import { CopilotLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const copilot: LLMProviderConfig = {
  id: 'copilot',
  name: 'GitHub Copilot',
  icon: CopilotLogo,
  description: 'Copilot subscription models via device code',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'gpt-4.1',
  // Context windows follow the curated AVA catalog for Copilot-backed models.
  models: [
    // ── OpenAI ──
    {
      id: 'gpt-4.1',
      name: 'GPT-4.1',
      contextWindow: 64000,
      isDefault: true,
      capabilities: ['vision', 'tools'],
    },
    { id: 'gpt-4o', name: 'GPT-4o', contextWindow: 64000, capabilities: ['vision', 'tools'] },
    {
      id: 'gpt-5',
      name: 'GPT-5',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5-mini',
      name: 'GPT-5 Mini',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.1',
      name: 'GPT-5.1',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.1-codex',
      name: 'GPT-5.1 Codex',
      contextWindow: 128000,
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'gpt-5.1-codex-max',
      name: 'GPT-5.1 Codex Max',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.1-codex-mini',
      name: 'GPT-5.1 Codex Mini',
      contextWindow: 128000,
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'gpt-5.2',
      name: 'GPT-5.2',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gpt-5.2-codex',
      name: 'GPT-5.2 Codex',
      contextWindow: 272000,
      capabilities: ['tools', 'reasoning'],
    },
    // ── Anthropic ──
    {
      id: 'claude-haiku-4.5',
      name: 'Claude Haiku 4.5',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-sonnet-4',
      name: 'Claude Sonnet 4',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-sonnet-4.5',
      name: 'Claude Sonnet 4.5',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-sonnet-4.6',
      name: 'Claude Sonnet 4.6',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-opus-4.1',
      name: 'Claude Opus 4.1',
      contextWindow: 80000,
      capabilities: ['vision', 'reasoning'],
    },
    {
      id: 'claude-opus-4.5',
      name: 'Claude Opus 4.5',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-opus-4.6',
      name: 'Claude Opus 4.6',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    // ── Google ──
    {
      id: 'gemini-2.5-pro',
      name: 'Gemini 2.5 Pro',
      contextWindow: 128000,
      capabilities: ['vision', 'tools'],
    },
    {
      id: 'gemini-3-flash-preview',
      name: 'Gemini 3 Flash',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gemini-3-pro-preview',
      name: 'Gemini 3 Pro',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'gemini-3.1-pro-preview',
      name: 'Gemini 3.1 Pro',
      contextWindow: 128000,
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    // ── xAI ──
    {
      id: 'grok-code-fast-1',
      name: 'Grok Code Fast 1',
      contextWindow: 128000,
      capabilities: ['tools', 'reasoning'],
    },
  ],
}
