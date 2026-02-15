/**
 * Credentials Manager
 *
 * Type-safe wrapper around platform credential store for API key management.
 * Uses OS keychain/credential manager for secure storage.
 *
 * Usage:
 * ```ts
 * const credentials = new CredentialsManager()
 *
 * // Store API key
 * await credentials.setApiKey('anthropic', 'sk-ant-...')
 *
 * // Retrieve API key
 * const key = await credentials.getApiKey('anthropic')
 *
 * // Check if provider has key
 * const hasKey = await credentials.hasApiKey('anthropic')
 *
 * // List all providers with keys
 * const providers = await credentials.listProviders()
 * ```
 */

import { getPlatform } from '../platform.js'
import type { CredentialKey, CredentialProvider, CredentialProviderInfo } from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Key prefix for all AVA credentials */
const KEY_PREFIX = 'ava'

/** API key suffix */
const API_KEY_SUFFIX = 'api_key'

/** All known credential providers */
export const KNOWN_PROVIDERS: CredentialProvider[] = [
  'anthropic',
  'openai',
  'openrouter',
  'google',
  'cohere',
  'mistral',
]

/** Provider display names */
export const PROVIDER_NAMES: Record<CredentialProvider, string> = {
  anthropic: 'Anthropic',
  openai: 'OpenAI',
  openrouter: 'OpenRouter',
  google: 'Google AI',
  cohere: 'Cohere',
  mistral: 'Mistral AI',
}

/** API key validation patterns */
export const KEY_PATTERNS: Partial<Record<CredentialProvider, RegExp>> = {
  anthropic: /^sk-ant-[A-Za-z0-9_-]+$/,
  openai: /^sk-[A-Za-z0-9_-]+$/,
  openrouter: /^sk-or-[A-Za-z0-9_-]+$/,
  google: /^AI[A-Za-z0-9_-]+$/,
}

// ============================================================================
// Credentials Manager
// ============================================================================

/**
 * Manages API keys and credentials using platform secure storage
 */
export class CredentialsManager {
  // ==========================================================================
  // Key Operations
  // ==========================================================================

  /**
   * Get API key for a provider
   * Returns null if not set
   */
  async getApiKey(provider: CredentialProvider): Promise<string | null> {
    const store = getPlatform().credentials
    const key = this.makeKey(provider)
    return store.get(key)
  }

  /**
   * Set API key for a provider
   * Validates key format if pattern available
   */
  async setApiKey(provider: CredentialProvider, apiKey: string): Promise<void> {
    // Validate key format
    const pattern = KEY_PATTERNS[provider]
    if (pattern && !pattern.test(apiKey)) {
      throw new CredentialValidationError(
        provider,
        `Invalid API key format for ${PROVIDER_NAMES[provider]}`
      )
    }

    const store = getPlatform().credentials
    const key = this.makeKey(provider)
    await store.set(key, apiKey)
  }

  /**
   * Delete API key for a provider
   */
  async deleteApiKey(provider: CredentialProvider): Promise<void> {
    const store = getPlatform().credentials
    const key = this.makeKey(provider)
    await store.delete(key)
  }

  /**
   * Check if provider has an API key set
   */
  async hasApiKey(provider: CredentialProvider): Promise<boolean> {
    const store = getPlatform().credentials
    const key = this.makeKey(provider)
    return store.has(key)
  }

  // ==========================================================================
  // Provider Operations
  // ==========================================================================

  /**
   * List all known providers with their key status
   */
  async listProviders(): Promise<CredentialProviderInfo[]> {
    const results: CredentialProviderInfo[] = []

    for (const provider of KNOWN_PROVIDERS) {
      const hasKey = await this.hasApiKey(provider)
      results.push({
        provider,
        name: PROVIDER_NAMES[provider],
        hasKey,
        keyPattern: KEY_PATTERNS[provider],
      })
    }

    return results
  }

  /**
   * Get all providers that have API keys configured
   */
  async getConfiguredProviders(): Promise<CredentialProvider[]> {
    const configured: CredentialProvider[] = []

    for (const provider of KNOWN_PROVIDERS) {
      if (await this.hasApiKey(provider)) {
        configured.push(provider)
      }
    }

    return configured
  }

  /**
   * Check if any provider has an API key configured
   */
  async hasAnyApiKey(): Promise<boolean> {
    for (const provider of KNOWN_PROVIDERS) {
      if (await this.hasApiKey(provider)) {
        return true
      }
    }
    return false
  }

  // ==========================================================================
  // Validation
  // ==========================================================================

  /**
   * Validate an API key format without storing it
   */
  validateApiKey(provider: CredentialProvider, apiKey: string): boolean {
    const pattern = KEY_PATTERNS[provider]
    if (!pattern) {
      // No pattern = accept any non-empty string
      return apiKey.length > 0
    }
    return pattern.test(apiKey)
  }

  /**
   * Get the expected key format description
   */
  getKeyFormatHint(provider: CredentialProvider): string {
    switch (provider) {
      case 'anthropic':
        return 'sk-ant-... (starts with sk-ant-)'
      case 'openai':
        return 'sk-... (starts with sk-)'
      case 'openrouter':
        return 'sk-or-... (starts with sk-or-)'
      case 'google':
        return 'AI... (starts with AI)'
      default:
        return 'API key'
    }
  }

  // ==========================================================================
  // Helpers
  // ==========================================================================

  /**
   * Create credential store key for a provider
   */
  private makeKey(provider: CredentialProvider): CredentialKey {
    return `${KEY_PREFIX}:${provider}:${API_KEY_SUFFIX}`
  }
}

// ============================================================================
// Validation Error
// ============================================================================

/**
 * Error thrown when credential validation fails
 */
export class CredentialValidationError extends Error {
  constructor(
    public readonly provider: CredentialProvider,
    message: string
  ) {
    super(message)
    this.name = 'CredentialValidationError'
  }
}

// ============================================================================
// Singleton Instance
// ============================================================================

let _instance: CredentialsManager | null = null

/**
 * Get the global credentials manager instance
 */
export function getCredentialsManager(): CredentialsManager {
  if (!_instance) {
    _instance = new CredentialsManager()
  }
  return _instance
}

/**
 * Set the global credentials manager instance (for testing)
 */
export function setCredentialsManager(manager: CredentialsManager | null): void {
  _instance = manager
}

// ============================================================================
// Factory Function
// ============================================================================

/**
 * Create a new credentials manager instance
 */
export function createCredentialsManager(): CredentialsManager {
  return new CredentialsManager()
}
