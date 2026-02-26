/**
 * Tool types — definitions, results, context.
 */

import type { ToolDefinition } from '../llm/types.js'

// ─── Tool Context ────────────────────────────────────────────────────────────

export interface MetadataUpdate<T = Record<string, unknown>> {
  title?: string
  metadata: T
}

export type MetadataCallback = (update: MetadataUpdate) => void

export interface ToolContext {
  sessionId: string
  workingDirectory: string
  signal: AbortSignal
  metadata?: MetadataCallback
}

// ─── Tool Location ───────────────────────────────────────────────────────────

export interface ToolLocation {
  path: string
  type: 'read' | 'write' | 'delete' | 'exec'
  lines?: [number, number]
}

// ─── Tool Result ─────────────────────────────────────────────────────────────

export interface ToolResult {
  success: boolean
  output: string
  metadata?: Record<string, unknown>
  error?: string
  locations?: ToolLocation[]
}

// ─── Tool Interface ──────────────────────────────────────────────────────────

export interface Tool<TParams = Record<string, unknown>> {
  definition: ToolDefinition
  validate?(params: unknown): TParams
  execute(params: TParams, ctx: ToolContext): Promise<ToolResult>
}

// biome-ignore lint: allow any for generic tool collections
export type AnyTool = Tool<any>
