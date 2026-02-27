/**
 * Models extension — model registry with capabilities and pricing.
 *
 * Listens for provider:registered events and populates a model registry.
 * Other extensions query the registry to find suitable models.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
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

  // Store registry for other extensions to query
  void api.storage.set('registry', Object.fromEntries(registry.models))

  api.emit('models:ready', { count: registry.models.size })
  api.log.debug('Models extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
