/**
 * Zhipu (GLM) Provider — Chinese bilingual AI
 * https://open.bigmodel.cn/dev/api
 */

import { GLMLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const glm: LLMProviderConfig = {
  id: 'glm',
  name: 'Zhipu (GLM)',
  icon: GLMLogo,
  description: 'Chinese AI with bilingual capabilities',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'glm-4-plus',
  models: [
    {
      id: 'glm-4-plus',
      name: 'GLM-4 Plus',
      contextWindow: 128000,
      isDefault: true,
      capabilities: ['tools'],
    },
    {
      id: 'glm-4-flash',
      name: 'GLM-4 Flash',
      contextWindow: 128000,
      capabilities: ['tools'],
    },
  ],
}
