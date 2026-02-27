/**
 * Provider Defaults
 *
 * Canonical types for LLM providers + re-export of default configurations.
 * Individual provider definitions live in ./providers/<name>.ts
 */

import type { Component } from 'solid-js'

// ============================================================================
// Types
// ============================================================================

export type IconComponent = Component<{ class?: string }>

export interface ProviderModel {
  id: string
  name: string
  contextWindow: number
  isDefault?: boolean
  pricing?: { input?: number; output?: number }
  capabilities?: string[]
}

export interface LLMProviderConfig {
  id: string
  name: string
  icon: IconComponent
  description: string
  enabled: boolean
  apiKey?: string
  baseUrl?: string
  defaultModel?: string
  models: ProviderModel[]
  status: 'connected' | 'disconnected' | 'error'
  error?: string
}

// ============================================================================
// Default Providers (re-exported from per-provider files)
// ============================================================================

export { defaultProviders } from './providers/index'
