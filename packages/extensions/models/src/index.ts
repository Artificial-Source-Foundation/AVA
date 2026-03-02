/**
 * Models extension — model registry with capabilities and pricing.
 *
 * Listens for provider:registered events and populates a model registry.
 * Other extensions query the registry to find suitable models.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { recordFailure, recordSuccess } from './availability.js'
import { addModelsToRegistry, createModelRegistry } from './registry.js'
import type { ModelInfo } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  const registry = createModelRegistry()
  const disposables: Disposable[] = []

  // Listen for providers registering their models
  disposables.push(
    api.on('provider:registered', (data) => {
      const { models } = data as { provider: string; models?: ModelInfo[] }
      if (models?.length) {
        addModelsToRegistry(registry, models)
        api.emit('models:updated', { count: registry.models.size })
      }
    })
  )

  // Listen for direct model registration
  disposables.push(
    api.on('models:register', (data) => {
      const models = data as ModelInfo[]
      addModelsToRegistry(registry, models)
      api.emit('models:updated', { count: registry.models.size })
    })
  )

  // Track model availability via LLM events
  disposables.push(
    api.on('llm:response', (data) => {
      const event = data as { provider: string; model: string; latencyMs: number }
      if (event.provider && event.model) {
        recordSuccess(event.provider, event.model, event.latencyMs)
      }
    })
  )

  disposables.push(
    api.on('llm:error', (data) => {
      const event = data as { provider: string; model: string; error: string }
      if (event.provider && event.model) {
        recordFailure(event.provider, event.model, event.error)
      }
    })
  )

  // Store registry for other extensions to query
  void api.storage.set('registry', Object.fromEntries(registry.models))

  api.emit('models:ready', { count: registry.models.size })
  api.log.debug('Models extension activated (availability tracking enabled)')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
