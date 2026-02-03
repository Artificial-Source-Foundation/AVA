/**
 * Tool System Types
 * Type definitions for LLM tool integration
 */

import type { ToolDefinition } from '../types/llm.js'

// Re-export ToolDefinition for consumers
export type { ToolDefinition } from '../types/llm.js'

// ============================================================================
// Tool Execution Types
// ============================================================================

/** Context passed to tool execution */
export interface ToolContext {
  sessionId: string
  workingDirectory: string
  signal: AbortSignal
}

/** Location affected by a tool operation */
export interface ToolLocation {
  path: string
  type: 'read' | 'write' | 'delete' | 'exec'
  lines?: [number, number] // Start, end lines
}

/** Result returned from tool execution */
export interface ToolResult {
  success: boolean
  output: string
  metadata?: Record<string, unknown>
  error?: string
  /** Paths affected by this tool operation */
  locations?: ToolLocation[]
}

/** Tool implementation interface */
export interface Tool<TParams = Record<string, unknown>> {
  definition: ToolDefinition
  validate?(params: unknown): TParams
  execute(params: TParams, ctx: ToolContext): Promise<ToolResult>
}
