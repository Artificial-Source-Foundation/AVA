/**
 * LLM client registry.
 *
 * Interface only — no provider implementations. Extensions register
 * providers via `api.registerProvider()` which calls `registerProvider()` here.
 */

import { createLogger } from '../logger/logger.js'
import { getPlatform } from '../platform.js'
import type { AuthInfo, LLMClient, LLMProvider } from './types.js'

const log = createLogger('LLM')

// ─── Provider Registry ───────────────────────────────────────────────────────

type ClientFactory = () => LLMClient
const providerRegistry = new Map<string, ClientFactory>()

export function registerProvider(name: string, factory: ClientFactory): void {
  providerRegistry.set(name, factory)
  log.debug(`Provider registered: ${name}`)
}

export function unregisterProvider(name: string): void {
  providerRegistry.delete(name)
}

export function createClient(provider: string): LLMClient {
  const factory = providerRegistry.get(provider)
  if (!factory) {
    throw new Error(
      `No LLM provider registered for "${provider}". ` +
        `Available: ${[...providerRegistry.keys()].join(', ') || 'none'}`
    )
  }
  return factory()
}

export function getRegisteredProviders(): string[] {
  return [...providerRegistry.keys()]
}

export function hasProvider(name: string): boolean {
  return providerRegistry.has(name)
}

export function resetProviders(): void {
  providerRegistry.clear()
}

// ─── Credential Resolution ───────────────────────────────────────────────────

export async function getApiKey(provider: LLMProvider): Promise<string | null> {
  const platform = getPlatform()
  return platform.credentials.get(`ava:${provider}:api_key`)
}

export async function getAuth(provider: LLMProvider): Promise<AuthInfo | null> {
  // Try OAuth first
  const oauthToken = await getPlatform().credentials.get(`ava:${provider}:oauth_token`)
  if (oauthToken) {
    return { type: 'oauth', token: oauthToken }
  }

  // Fall back to API key
  const apiKey = await getApiKey(provider)
  if (apiKey) {
    return { type: 'api-key', token: apiKey }
  }

  return null
}
