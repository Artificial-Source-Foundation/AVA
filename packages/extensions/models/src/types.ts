/**
 * Model registry types.
 */

export interface ModelInfo {
  id: string
  provider: string
  displayName: string
  contextWindow: number
  maxOutput: number
  supportsTools: boolean
  supportsVision: boolean
  pricing?: { inputPer1M: number; outputPer1M: number }
}

export interface ModelRegistry {
  models: Map<string, ModelInfo>
  getModel(id: string): ModelInfo | undefined
  getModelsForProvider(provider: string): ModelInfo[]
}
