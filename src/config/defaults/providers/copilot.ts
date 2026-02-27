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
  models: [
    {
      id: 'gpt-4.1',
      name: 'GPT-4.1',
      contextWindow: 1000000,
      isDefault: true,
      capabilities: ['vision', 'tools'],
    },
    {
      id: 'gpt-4o',
      name: 'GPT-4o',
      contextWindow: 128000,
      capabilities: ['vision', 'tools'],
    },
    {
      id: 'claude-3.5-sonnet',
      name: 'Claude 3.5 Sonnet',
      contextWindow: 200000,
      capabilities: ['vision', 'tools'],
    },
    {
      id: 'o3-mini',
      name: 'o3 Mini',
      contextWindow: 200000,
      capabilities: ['tools', 'reasoning'],
    },
  ],
}
