/**
 * Model registry — tracks available models across providers.
 */

import type { ModelInfo, ModelRegistry } from './types.js'

export function createModelRegistry(): ModelRegistry {
  const models = new Map<string, ModelInfo>()

  return {
    models,

    getModel(id: string): ModelInfo | undefined {
      return models.get(id)
    },

    getModelsForProvider(provider: string): ModelInfo[] {
      const result: ModelInfo[] = []
      for (const model of models.values()) {
        if (model.provider === provider) result.push(model)
      }
      return result
    },
  }
}

export function addModelsToRegistry(registry: ModelRegistry, newModels: ModelInfo[]): void {
  for (const model of newModels) {
    registry.models.set(model.id, model)
  }
}
