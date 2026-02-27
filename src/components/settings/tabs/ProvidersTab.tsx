/**
 * ProvidersTab — Re-export wrapper
 *
 * The ProvidersTab has been moved to ./providers/providers-tab.tsx.
 * This file exists for backward compatibility.
 */

export type { LLMProviderConfig, ProviderModel } from '../../../config/defaults/provider-defaults'
export { defaultProviders } from '../../../config/defaults/provider-defaults'
export type { ProvidersTabProps } from './providers/providers-tab'
export { ProvidersTab } from './providers/providers-tab'
