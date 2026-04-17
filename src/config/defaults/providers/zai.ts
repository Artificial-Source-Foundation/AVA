/**
 * Z.AI Coding Plan provider
 */

import { ZAILogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const zai: LLMProviderConfig = {
  id: 'zai',
  name: 'Z.AI',
  icon: ZAILogo,
  description: 'ZhipuAI coding plan via z.ai',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'glm-4.7',
  models: [
    {
      id: 'glm-4.7',
      name: 'GLM-4.7',
      contextWindow: 128000,
      isDefault: true,
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'glm-5',
      name: 'GLM-5',
      contextWindow: 128000,
      capabilities: ['tools', 'reasoning'],
    },
  ],
}
