/**
 * OpenRouter Provider — Unified access to 300+ models
 * https://openrouter.ai/docs
 * Last updated: Feb 2026 — only models with tool/function calling support
 */

import { OpenRouterLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const openrouter: LLMProviderConfig = {
  id: 'openrouter',
  name: 'OpenRouter',
  icon: OpenRouterLogo,
  description: 'Access 300+ models via single API',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'anthropic/claude-sonnet-4.6',
  models: [
    // ── Frontier ───────────────────────────────────────────
    {
      id: 'anthropic/claude-opus-4.6',
      name: 'Claude Opus 4.6',
      contextWindow: 1000000,
      pricing: { input: 5, output: 25 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'anthropic/claude-sonnet-4.6',
      name: 'Claude Sonnet 4.6',
      contextWindow: 1000000,
      isDefault: true,
      pricing: { input: 3, output: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'openai/gpt-5.3-codex',
      name: 'GPT-5.3 Codex',
      contextWindow: 400000,
      pricing: { input: 1.75, output: 14 },
      capabilities: ['vision', 'tools', 'reasoning', 'thinking'],
    },
    {
      id: 'openai/gpt-5.2',
      name: 'GPT-5.2',
      contextWindow: 400000,
      pricing: { input: 1.75, output: 14 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'google/gemini-3-pro-preview',
      name: 'Gemini 3 Pro',
      contextWindow: 1048576,
      pricing: { input: 2, output: 12 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    // ── Strong General-Purpose ─────────────────────────────
    {
      id: 'anthropic/claude-sonnet-4',
      name: 'Claude Sonnet 4',
      contextWindow: 1000000,
      pricing: { input: 3, output: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'openai/gpt-5.1',
      name: 'GPT-5.1',
      contextWindow: 400000,
      pricing: { input: 1.25, output: 10 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'google/gemini-2.5-pro',
      name: 'Gemini 2.5 Pro',
      contextWindow: 1048576,
      pricing: { input: 1.25, output: 10 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    // ── Fast / Cost-Efficient ──────────────────────────────
    {
      id: 'anthropic/claude-haiku-4.5',
      name: 'Claude Haiku 4.5',
      contextWindow: 200000,
      pricing: { input: 1, output: 5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'google/gemini-3-flash-preview',
      name: 'Gemini 3 Flash',
      contextWindow: 1048576,
      pricing: { input: 0.5, output: 3 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'google/gemini-2.5-flash',
      name: 'Gemini 2.5 Flash',
      contextWindow: 1048576,
      pricing: { input: 0.3, output: 2.5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'mistralai/mistral-large-2512',
      name: 'Mistral Large 3',
      contextWindow: 262144,
      pricing: { input: 0.5, output: 1.5 },
      capabilities: ['vision', 'tools'],
    },
    {
      id: 'mistralai/devstral-2512',
      name: 'Devstral 2',
      contextWindow: 262144,
      pricing: { input: 0.4, output: 2 },
      capabilities: ['tools'],
    },
    // ── Budget / Open-Source ───────────────────────────────
    {
      id: 'deepseek/deepseek-v3.2',
      name: 'DeepSeek V3.2',
      contextWindow: 163840,
      pricing: { input: 0.25, output: 0.4 },
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'x-ai/grok-4.1-fast',
      name: 'Grok 4.1 Fast',
      contextWindow: 2000000,
      pricing: { input: 0.2, output: 0.5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'meta-llama/llama-4-maverick',
      name: 'Llama 4 Maverick',
      contextWindow: 1048576,
      pricing: { input: 0.15, output: 0.6 },
      capabilities: ['vision', 'tools'],
    },
    {
      id: 'cohere/command-a',
      name: 'Command A',
      contextWindow: 256000,
      pricing: { input: 2.5, output: 10 },
      capabilities: ['tools'],
    },
  ],
}
