/**
 * Credential Management Service
 * Handles storage and retrieval of API keys and OAuth tokens
 */

import type { Credentials, LLMProvider, StoredCredentials } from '../../types/llm'

const STORAGE_KEY = 'estela_credentials'

// ============================================================================
// Core Functions
// ============================================================================

/**
 * Get credentials for a specific provider
 * Returns null if not found or expired (for OAuth tokens)
 */
export function getCredentials(provider: LLMProvider): Credentials | null {
  const stored = localStorage.getItem(STORAGE_KEY)
  if (!stored) return null

  try {
    const all: StoredCredentials = JSON.parse(stored)
    const cred = all[provider]
    if (!cred) return null

    // Check expiry for OAuth tokens
    if (cred.type === 'oauth-token' && cred.expiresAt) {
      if (Date.now() > cred.expiresAt) {
        // Token expired - could trigger refresh here in future
        console.warn(`OAuth token expired for ${provider}`)
        return null
      }
    }

    return cred
  } catch (err) {
    console.error('Failed to parse credentials:', err)
    return null
  }
}

/**
 * Store credentials for a provider
 * Overwrites existing credentials for that provider
 */
function setCredentials(provider: LLMProvider, credentials: Credentials): void {
  const stored = localStorage.getItem(STORAGE_KEY)
  let all: StoredCredentials = {}

  try {
    if (stored) {
      all = JSON.parse(stored)
    }
  } catch {
    // Start fresh if corrupted
    all = {}
  }

  all[provider] = credentials
  localStorage.setItem(STORAGE_KEY, JSON.stringify(all))
}

/**
 * Remove credentials for a provider
 */
export function clearCredentials(provider: LLMProvider): void {
  const stored = localStorage.getItem(STORAGE_KEY)
  if (!stored) return

  try {
    const all: StoredCredentials = JSON.parse(stored)
    delete all[provider]
    localStorage.setItem(STORAGE_KEY, JSON.stringify(all))
  } catch {
    // If corrupted, clear everything
    localStorage.removeItem(STORAGE_KEY)
  }
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Set API key for a provider (convenience wrapper)
 */
export function setApiKey(provider: LLMProvider, apiKey: string): void {
  setCredentials(provider, {
    provider,
    type: 'api-key',
    value: apiKey,
  })
}

/**
 * Get API key for a provider (convenience wrapper)
 * Returns null if not found or if it's an OAuth token
 */
export function getApiKey(provider: LLMProvider): string | null {
  const cred = getCredentials(provider)
  if (!cred || cred.type !== 'api-key') return null
  return cred.value
}

// ============================================================================
// Environment Variable Fallback
// ============================================================================

/**
 * Get API key with environment variable fallback
 * Useful for development
 */
export function getApiKeyWithFallback(provider: LLMProvider): string | null {
  // First check localStorage
  const stored = getApiKey(provider)
  if (stored) return stored

  // Fallback to environment variables (ESTELA_ prefix for consistency with CLI)
  const envKeys: Record<LLMProvider, string> = {
    openrouter: 'VITE_OPENROUTER_API_KEY',
    anthropic: 'VITE_ANTHROPIC_API_KEY',
    openai: 'VITE_OPENAI_API_KEY',
    google: 'VITE_GOOGLE_API_KEY',
    copilot: 'VITE_COPILOT_API_KEY',
    glm: 'VITE_GLM_API_KEY',
    kimi: 'VITE_KIMI_API_KEY',
  }

  const envKey = envKeys[provider]
  return import.meta.env[envKey] || null
}
