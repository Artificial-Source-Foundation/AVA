/**
 * Config Types
 *
 * Type definitions for application settings and configuration.
 * Settings are organized by category for easy access and updates.
 */

import type { LLMProvider } from '../types/llm.js'

// Re-export for convenience (but allow both sources)
export type { LLMProvider }

// ============================================================================
// Provider Settings
// ============================================================================

/** Provider-related settings */
export interface ProviderSettings {
  /** Default LLM provider */
  defaultProvider: LLMProvider
  /** Default model for the selected provider */
  defaultModel: string
  /** Request timeout in milliseconds */
  timeout: number
  /** Fall back to OpenRouter if direct API fails */
  openRouterFallback: boolean
  /** Custom API base URLs (e.g., for local models) */
  customEndpoints?: Record<string, string>
}

// ============================================================================
// Agent Settings
// ============================================================================

/** Agent execution settings */
export interface AgentSettings {
  /** Maximum turns before stopping */
  maxTurns: number
  /** Maximum execution time in minutes */
  maxTimeMinutes: number
  /** Maximum retries on recoverable errors */
  maxRetries: number
  /** Enable validation pipeline after changes */
  validatorsEnabled: boolean
  /** Which validators to run */
  enabledValidators: ValidatorType[]
  /** Maximum parallel workers for commander */
  parallelWorkers: number
  /** Grace period in milliseconds before timeout */
  gracePeriodMs: number
}

/** Available validator types */
export type ValidatorType = 'syntax' | 'typescript' | 'lint' | 'test' | 'selfReview'

// ============================================================================
// Permission Settings
// ============================================================================

/** Permission and safety settings */
export interface PermissionSettings {
  /** Glob patterns to auto-approve for file operations */
  autoApprovePatterns: string[]
  /** Always require user confirmation for these actions */
  requireConfirmation: ('delete' | 'execute' | 'write' | 'network')[]
  /** Paths that are always denied (never modified) */
  deniedPaths: string[]
  /** Maximum file size to read (bytes) */
  maxReadSize: number
  /** Allow bash command execution */
  allowBashExecution: boolean
  /** Allow network requests */
  allowNetworkRequests: boolean
}

// ============================================================================
// Context Settings
// ============================================================================

/** Context and token management settings */
export interface ContextSettings {
  /** Maximum tokens before compaction triggers */
  maxTokens: number
  /** Percentage of maxTokens that triggers compaction */
  compactionThreshold: number
  /** Enable auto-save of sessions */
  autoSave: boolean
  /** Auto-save interval in milliseconds (0 = disabled) */
  autoSaveInterval: number
  /** Checkpoint interval in messages (0 = disabled) */
  checkpointInterval: number
  /** Maximum sessions to keep in memory */
  maxSessions: number
}

// ============================================================================
// Memory Settings
// ============================================================================

/** Long-term memory settings */
export interface MemorySettings {
  /** Enable long-term memory */
  enabled: boolean
  /** Embedding model to use */
  embeddingModel: string
  /** Maximum memories to retrieve per query */
  maxRetrievals: number
  /** Minimum similarity score for retrieval (0-1) */
  minSimilarity: number
  /** Memory consolidation interval in hours (0 = manual only) */
  consolidationInterval: number
  /** Decay rate for importance (lambda in exponential decay) */
  decayRate: number
  /** Maximum memories to store (oldest/lowest importance removed first) */
  maxMemories: number
}

// ============================================================================
// UI Settings
// ============================================================================

/** User interface settings */
export interface UISettings {
  /** Color theme */
  theme: 'light' | 'dark' | 'system'
  /** Font size in pixels */
  fontSize: number
  /** Show token count in UI */
  showTokenCounts: boolean
  /** Streaming speed (characters per frame) */
  streamingSpeed: number
  /** Enable syntax highlighting */
  syntaxHighlighting: boolean
  /** Enable line numbers in code blocks */
  lineNumbers: boolean
  /** Compact mode (less padding) */
  compactMode: boolean
}

// ============================================================================
// Combined Settings
// ============================================================================

/** Complete application settings */
export interface Settings {
  provider: ProviderSettings
  agent: AgentSettings
  permissions: PermissionSettings
  context: ContextSettings
  memory: MemorySettings
  ui: UISettings
}

/** Settings category keys */
export type SettingsCategory = keyof Settings

// ============================================================================
// Default Values
// ============================================================================

/** Default provider settings */
export const DEFAULT_PROVIDER_SETTINGS: ProviderSettings = {
  defaultProvider: 'anthropic',
  defaultModel: 'claude-sonnet-4-20250514',
  timeout: 120000, // 2 minutes
  openRouterFallback: true,
}

/** Default agent settings */
export const DEFAULT_AGENT_SETTINGS: AgentSettings = {
  maxTurns: 50,
  maxTimeMinutes: 30,
  maxRetries: 3,
  validatorsEnabled: true,
  enabledValidators: ['syntax', 'typescript', 'lint'],
  parallelWorkers: 4,
  gracePeriodMs: 60000, // 1 minute
}

/** Default permission settings */
export const DEFAULT_PERMISSION_SETTINGS: PermissionSettings = {
  autoApprovePatterns: [],
  requireConfirmation: ['delete', 'execute'],
  deniedPaths: ['~/.ssh', '~/.gnupg', '~/.aws/credentials', '.env.local'],
  maxReadSize: 10 * 1024 * 1024, // 10 MB
  allowBashExecution: true,
  allowNetworkRequests: true,
}

/** Default context settings */
export const DEFAULT_CONTEXT_SETTINGS: ContextSettings = {
  maxTokens: 200000,
  compactionThreshold: 80, // 80% of maxTokens
  autoSave: true,
  autoSaveInterval: 60000, // 1 minute
  checkpointInterval: 10, // Every 10 messages
  maxSessions: 10,
}

/** Default memory settings */
export const DEFAULT_MEMORY_SETTINGS: MemorySettings = {
  enabled: true,
  embeddingModel: 'text-embedding-3-small',
  maxRetrievals: 5,
  minSimilarity: 0.7,
  consolidationInterval: 24, // Daily
  decayRate: 0.001,
  maxMemories: 10000,
}

/** Default UI settings */
export const DEFAULT_UI_SETTINGS: UISettings = {
  theme: 'system',
  fontSize: 14,
  showTokenCounts: true,
  streamingSpeed: 50,
  syntaxHighlighting: true,
  lineNumbers: true,
  compactMode: false,
}

/** Default complete settings */
export const DEFAULT_SETTINGS: Settings = {
  provider: DEFAULT_PROVIDER_SETTINGS,
  agent: DEFAULT_AGENT_SETTINGS,
  permissions: DEFAULT_PERMISSION_SETTINGS,
  context: DEFAULT_CONTEXT_SETTINGS,
  memory: DEFAULT_MEMORY_SETTINGS,
  ui: DEFAULT_UI_SETTINGS,
}

// ============================================================================
// Event Types
// ============================================================================

/** Settings change event */
export type SettingsEvent =
  | { type: 'settings_loaded' }
  | { type: 'settings_saved' }
  | { type: 'settings_reset'; category?: SettingsCategory }
  | { type: 'category_changed'; category: SettingsCategory }

/** Settings event listener */
export type SettingsEventListener = (event: SettingsEvent) => void

// ============================================================================
// Credential Types
// ============================================================================

/** Known API key providers */
export type CredentialProvider =
  | 'anthropic'
  | 'openai'
  | 'openrouter'
  | 'google'
  | 'cohere'
  | 'mistral'

/** Credential key format: estela:{provider}:api_key */
export type CredentialKey = `estela:${CredentialProvider}:api_key`

/** Credential provider info */
export interface CredentialProviderInfo {
  provider: CredentialProvider
  name: string
  hasKey: boolean
  keyPattern?: RegExp
}

// ============================================================================
// Export/Import Types
// ============================================================================

/** Exportable settings (excludes API keys) */
export interface ExportableSettings {
  version: number
  exportedAt: string
  settings: Settings
}

/** Settings file format version */
export const SETTINGS_VERSION = 1
