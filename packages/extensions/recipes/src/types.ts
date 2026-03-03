/**
 * Recipe/workflow system types.
 *
 * YAML/JSON-based composable workflows with param substitution and scheduling.
 */

export interface RecipeParam {
  name: string
  description?: string
  default?: string
  required?: boolean
}

export interface RecipeStep {
  name: string
  tool?: string
  command?: string
  goal?: string
  recipe?: string
  args?: Record<string, string>
  parallel?: boolean
  condition?: string
  retry?: {
    maxAttempts: number
    delayMs?: number
  }
  onError?: 'continue' | 'abort'
}

export interface Recipe {
  name: string
  description?: string
  version?: string
  params?: RecipeParam[]
  steps: RecipeStep[]
  schedule?: string
}

export interface RecipeResult {
  recipe: string
  startedAt: number
  completedAt: number
  steps: Array<{
    name: string
    success: boolean
    result?: string
    error?: string
    duration: number
  }>
  success: boolean
}
