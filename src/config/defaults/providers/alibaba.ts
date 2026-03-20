/**
 * Alibaba Cloud Model Studio — Coding Plan
 * Multi-vendor models (Qwen, GLM, Kimi, MiniMax) via Anthropic-compatible API
 * https://www.alibabacloud.com/help/en/model-studio/opencode-coding-plan
 */

import { AlibabaCloudLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const alibaba: LLMProviderConfig = {
  id: 'alibaba',
  name: 'Alibaba Cloud',
  icon: AlibabaCloudLogo,
  description: 'Model Studio coding plan — Qwen, GLM, Kimi, MiniMax',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'qwen3-coder-plus',
  models: [
    // ── Qwen ──
    {
      id: 'qwen3.5-plus',
      name: 'Qwen3.5 Plus',
      contextWindow: 1000000,
      pricing: { input: 0.4, output: 1.2 },
      capabilities: ['tools', 'reasoning', 'vision'],
    },
    {
      id: 'qwen3-max-2026-01-23',
      name: 'Qwen3 Max',
      contextWindow: 1000000,
      pricing: { input: 1.6, output: 6.4 },
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'qwen3-coder-next',
      name: 'Qwen3 Coder Next',
      contextWindow: 262144,
      isDefault: true,
      pricing: { input: 0.5, output: 2.5 },
      capabilities: ['tools'],
    },
    {
      id: 'qwen3-coder-plus',
      name: 'Qwen3 Coder Plus',
      contextWindow: 1000000,
      pricing: { input: 0.5, output: 2.5 },
      capabilities: ['tools'],
    },
    // ── Zhipu ──
    {
      id: 'glm-5',
      name: 'GLM-5',
      contextWindow: 128000,
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'glm-4.7',
      name: 'GLM-4.7',
      contextWindow: 128000,
      capabilities: ['tools', 'reasoning'],
    },
    // ── Kimi ──
    {
      id: 'kimi-k2.5',
      name: 'Kimi K2.5',
      contextWindow: 131072,
      capabilities: ['tools', 'reasoning', 'vision'],
    },
    // ── MiniMax ──
    {
      id: 'MiniMax-M2.5',
      name: 'MiniMax M2.5',
      contextWindow: 1000000,
      capabilities: ['tools', 'reasoning'],
    },
  ],
}
