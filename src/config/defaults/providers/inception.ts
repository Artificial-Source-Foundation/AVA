/**
 * Inception Provider
 */

import { InceptionLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const inception: LLMProviderConfig = {
  id: 'inception',
  name: 'Inception',
  icon: InceptionLogo,
  description: 'Inception Labs coding models',
  enabled: false,
  status: 'disconnected',
  defaultModel: 'mercury-coder-small',
  models: [
    {
      id: 'mercury-coder-small',
      name: 'Mercury Coder Small',
      contextWindow: 128000,
      isDefault: true,
      capabilities: ['tools', 'reasoning'],
    },
  ],
}
