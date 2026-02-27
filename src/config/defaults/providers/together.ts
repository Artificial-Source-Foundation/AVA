/**
 * Together Provider — Open-source model hosting
 * https://docs.together.ai/docs/models
 */

import { TogetherLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const together: LLMProviderConfig = {
  id: 'together',
  name: 'Together',
  icon: TogetherLogo,
  description: 'Open-source models with fast inference',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'meta-llama/Llama-3.3-70B-Instruct-Turbo',
  models: [
    {
      id: 'meta-llama/Llama-3.3-70B-Instruct-Turbo',
      name: 'Llama 3.3 70B',
      contextWindow: 128000,
      isDefault: true,
      capabilities: ['tools'],
    },
    {
      id: 'mistralai/Mixtral-8x7B-Instruct-v0.1',
      name: 'Mixtral 8x7B',
      contextWindow: 32768,
      capabilities: ['tools'],
    },
    {
      id: 'Qwen/Qwen2.5-Coder-32B-Instruct',
      name: 'Qwen 2.5 Coder 32B',
      contextWindow: 32768,
      capabilities: ['tools'],
    },
  ],
}
