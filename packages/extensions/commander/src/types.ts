/**
 * Commander types — worker definitions and team hierarchy.
 */

export interface WorkerDefinition {
  name: string
  displayName: string
  description: string
  systemPrompt: string
  tools: string[]
  maxTurns?: number
  maxTimeMinutes?: number
}

export interface WorkerResult {
  success: boolean
  output: string
  tokensUsed: number
  durationMs: number
  turns: number
  error?: string
}

export interface TaskAnalysis {
  keywords: string[]
  taskType: TaskType
  confidence: number
}

export type TaskType = 'write' | 'test' | 'review' | 'research' | 'debug' | 'general'
