/**
 * Commander Types
 * Core types for hierarchical agent delegation
 *
 * Based on Gemini CLI's SubagentToolWrapper and OpenCode's Task tool patterns
 */

import type { AgentStep, AgentTerminateMode } from '../agent/types.js'

// ============================================================================
// Worker Definition
// ============================================================================

/**
 * Definition of a specialized worker agent
 */
export interface WorkerDefinition {
  /** Unique identifier (e.g., 'coder', 'tester') */
  name: string
  /** Human-readable name (e.g., 'Coder') */
  displayName: string
  /** Description for commander's "phone book" - helps LLM decide when to use */
  description: string
  /** Worker-specific system prompt */
  systemPrompt: string
  /** List of allowed tool names */
  tools: string[]
  /** Max conversation turns (default: 10) */
  maxTurns?: number
  /** Max execution time in minutes (default: 5) */
  maxTimeMinutes?: number
  /** Override model */
  model?: string
  /** Override provider */
  provider?: 'anthropic' | 'openai' | 'openrouter'
}

// ============================================================================
// Worker Inputs/Outputs
// ============================================================================

/**
 * Input passed to worker execution
 */
export interface WorkerInputs {
  /** The delegated task description */
  task: string
  /** Additional context from commander */
  context?: string
  /** Working directory */
  cwd: string
  /** Parent agent ID for tracking */
  parentAgentId?: string
}

/**
 * Result returned from worker execution
 */
export interface WorkerResult {
  /** Whether the worker completed successfully */
  success: boolean
  /** Final output from the worker */
  output: string
  /** How the worker terminated */
  terminateMode: AgentTerminateMode
  /** Total tokens used */
  tokensUsed: number
  /** Total duration in milliseconds */
  durationMs: number
  /** Number of turns taken */
  turns: number
  /** Error message if failed */
  error?: string
  /** All steps executed by the worker */
  steps?: AgentStep[]
}

// ============================================================================
// Activity Events
// ============================================================================

/**
 * Activity event types for worker execution
 */
export type WorkerActivityType =
  | 'thought'
  | 'tool:start'
  | 'tool:finish'
  | 'tool:error'
  | 'progress'
  | 'error'

/**
 * Activity event streamed during worker execution
 */
export interface WorkerActivityEvent {
  /** Event type */
  type: WorkerActivityType
  /** Worker name */
  workerName: string
  /** Timestamp */
  timestamp: number
  /** Event-specific data */
  data: Record<string, unknown>
}

/**
 * Callback for worker activity events
 */
export type WorkerActivityCallback = (event: WorkerActivityEvent) => void

// ============================================================================
// Combined Results
// ============================================================================

/**
 * Combined result from multiple workers
 */
export interface CombinedWorkerResult {
  /** Overall success (all workers succeeded) */
  success: boolean
  /** Summary of all worker outputs */
  summary: string
  /** Detailed output from each worker */
  details: string
  /** Individual worker results */
  results: Array<{
    worker: string
    result: WorkerResult
  }>
  /** Total tokens used across all workers */
  totalTokensUsed: number
  /** Total duration in milliseconds */
  totalDurationMs: number
}
