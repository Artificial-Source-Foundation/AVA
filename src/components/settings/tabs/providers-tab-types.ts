import type { LLMProviderConfig, ProviderModel } from '../../../config/defaults/provider-defaults'

export interface ProvidersTabProps {
  providers: LLMProviderConfig[]
  onToggle?: (id: string, enabled: boolean) => void
  onSaveApiKey?: (id: string, key: string) => void
  onClearApiKey?: (id: string) => void
  onSetDefaultModel?: (providerId: string, modelId: string) => void
  onTestConnection?: (id: string) => void
  onUpdateModels?: (providerId: string, models: ProviderModel[]) => void
}

export interface ProviderRowProps {
  provider: LLMProviderConfig
  isExpanded: boolean
  onExpand: () => void
  onToggle?: (enabled: boolean) => void
  onSaveApiKey?: (key: string) => void
  onClearApiKey?: () => void
  onSetDefaultModel?: (modelId: string) => void
  onTestConnection?: () => void
  onUpdateModels?: (models: ProviderModel[]) => void
}
