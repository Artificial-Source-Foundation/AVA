/**
 * Commander types — Praxis v2 hierarchy and worker compatibility types.
 */

// Re-export canonical types
export type { AgentDefinition, AgentTier } from './agent-definition.js'

export type AgentRole = 'director' | 'tech-lead' | 'engineer' | 'reviewer' | 'subagent'

export interface TierToolPolicy {
  allowed: string[]
  denied: string[]
}

/** @deprecated Use AgentDefinition instead */
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
