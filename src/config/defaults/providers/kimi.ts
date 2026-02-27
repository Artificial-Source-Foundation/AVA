/**
 * Kimi Provider — Moonshot AI long-context models
 * https://platform.moonshot.cn/docs
 */

import { KimiLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const kimi: LLMProviderConfig = {
  id: 'kimi',
  name: 'Kimi',
  icon: KimiLogo,
  description: 'Moonshot AI models with long context',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'moonshot-v1-128k',
  models: [
    {
      id: 'moonshot-v1-128k',
      name: 'Kimi v1 128K',
      contextWindow: 128000,
      isDefault: true,
      capabilities: ['tools'],
    },
  ],
}
