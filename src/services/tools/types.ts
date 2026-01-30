/**
 * Tool System Types
 * Type definitions for LLM tool integration
 */

// ============================================================================
// Tool Definition Types (Anthropic-compatible)
// ============================================================================

/** JSON Schema property for tool parameters */
interface ToolParameterProperty {
  type: 'string' | 'number' | 'boolean' | 'array' | 'object'
  description: string
  enum?: string[]
  items?: ToolParameterProperty
  default?: unknown
}

/** JSON Schema for tool parameters */
export interface ToolParameterSchema {
  type: 'object'
  properties: Record<string, ToolParameterProperty>
  required?: string[]
}

/** Tool definition sent to LLM */
export interface ToolDefinition {
  name: string
  description: string
  input_schema: ToolParameterSchema
}

// ============================================================================
// Tool Execution Types
// ============================================================================

/** Context passed to tool execution */
export interface ToolContext {
  sessionId: string
  workingDirectory: string
  signal: AbortSignal
}

/** Result returned from tool execution */
export interface ToolResult {
  success: boolean
  output: string
  metadata?: Record<string, unknown>
  error?: string
}

/** Tool implementation interface */
export interface Tool<TParams = Record<string, unknown>> {
  definition: ToolDefinition
  validate?(params: unknown): TParams
  execute(params: TParams, ctx: ToolContext): Promise<ToolResult>
}
