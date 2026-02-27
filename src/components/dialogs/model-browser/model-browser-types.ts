/**
 * Model Browser Types
 *
 * Types for the model browser dialog (chat model selector).
 */

import type { Accessor } from 'solid-js'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'

// ============================================================================
// Browsable Model (flattened from provider + model)
// ============================================================================

export interface BrowsableModel {
  id: string
  name: string
  providerId: string
  providerName: string
  contextWindow: number
  isDefault?: boolean
  pricing?: ModelPricing
  capabilities: ModelCapability[]
}

export interface ModelPricing {
  input?: number // per 1M tokens
  output?: number // per 1M tokens
}

export type ModelCapability = 'reasoning' | 'tools' | 'vision' | 'free'

// ============================================================================
// Filter State
// ============================================================================

export interface FilterState {
  search: string
  provider: string | null
  capabilities: ModelCapability[]
  sort: SortOption
}

export type SortOption = 'name' | 'context' | 'price'

// ============================================================================
// Component Props
// ============================================================================

export interface ModelBrowserDialogProps {
  open: Accessor<boolean>
  onOpenChange: (open: boolean) => void
  selectedModel: Accessor<string>
  onSelect: (modelId: string) => void
  enabledProviders: Accessor<LLMProviderConfig[]>
}
