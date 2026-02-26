/**
 * Provider test harness.
 *
 * Reusable test suite for verifying any LLM provider extension:
 * - Activation registers the provider
 * - Factory returns a client with stream() method
 * - Dispose cleans up the registration
 */

import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { describe, expect, it } from 'vitest'

type ActivateFn = (api: ExtensionAPI) => Disposable

export function testProviderActivation(activate: ActivateFn, providerName: string): void {
  describe(`${providerName} provider`, () => {
    it('activates and registers provider', () => {
      const { api, registeredProviders } = createMockExtensionAPI()
      const disposable = activate(api)
      expect(disposable).toBeDefined()
      expect(disposable.dispose).toBeTypeOf('function')
      expect(registeredProviders).toHaveLength(1)
      expect(registeredProviders[0].name).toBe(providerName)
    })

    it('factory returns client with stream method', () => {
      const { api, registeredProviders } = createMockExtensionAPI()
      activate(api)
      const factory = registeredProviders[0].factory
      expect(factory).toBeTypeOf('function')
      const client = factory()
      expect(client).toBeDefined()
      expect(client.stream).toBeTypeOf('function')
    })

    it('cleans up on dispose', () => {
      const { api, registeredProviders } = createMockExtensionAPI()
      const disposable = activate(api)
      expect(registeredProviders).toHaveLength(1)
      disposable.dispose()
      expect(registeredProviders).toHaveLength(0)
    })
  })
}
