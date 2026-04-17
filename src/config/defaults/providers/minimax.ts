/**
 * MiniMax Coding Plan provider
 */

import { MiniMaxLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const minimax: LLMProviderConfig = {
  id: 'minimax',
  name: 'MiniMax',
  icon: MiniMaxLogo,
  description: 'MiniMax coding plan',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'MiniMax-M2.5',
  models: [
    {
      id: 'MiniMax-M2.5',
      name: 'MiniMax M2.5',
      contextWindow: 1000000,
      isDefault: true,
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'MiniMax-M2',
      name: 'MiniMax M2',
      contextWindow: 1000000,
      capabilities: ['tools', 'reasoning'],
    },
  ],
}
