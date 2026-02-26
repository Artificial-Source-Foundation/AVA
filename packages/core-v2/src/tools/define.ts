/**
 * Tool definition helper with Zod validation.
 */

import * as z from 'zod'
import type { ToolDefinition } from '../llm/types.js'
import type { Tool, ToolContext, ToolLocation, ToolResult } from './types.js'

// ─── Types ───────────────────────────────────────────────────────────────────

export type ToolPermission = 'read' | 'write' | 'delete' | 'execute'

export interface ToolExample<T> {
  input: T
  description: string
}

export interface ToolConfig<TSchema extends z.ZodType> {
  name: string
  description: string
  schema: TSchema
  execute: (input: z.infer<TSchema>, ctx: ToolContext) => Promise<ToolResult>
  validate?: (params: unknown) => z.infer<TSchema>
  permissions?: ToolPermission[]
  locations?: (input: z.infer<TSchema>) => ToolLocation[]
  examples?: ToolExample<z.infer<TSchema>>[]
}

export interface DefinedTool<TInput = unknown> extends Tool<TInput> {
  permissions?: ToolPermission[]
  getLocations?: (input: TInput) => ToolLocation[]
  examples?: ToolExample<TInput>[]
}

// ─── defineTool ──────────────────────────────────────────────────────────────

export function defineTool<TSchema extends z.ZodType>(
  config: ToolConfig<TSchema>
): DefinedTool<z.infer<TSchema>> {
  const jsonSchema = z.toJSONSchema(config.schema, { unrepresentable: 'any' })

  const definition: ToolDefinition = {
    name: config.name,
    description: config.description,
    input_schema: {
      type: 'object',
      properties:
        ((jsonSchema as Record<string, unknown>).properties as Record<string, unknown>) ?? {},
      required: (jsonSchema as Record<string, unknown>).required as string[] | undefined,
    },
  }

  return {
    definition,
    validate: config.validate ?? ((params: unknown) => z.parse(config.schema, params)),
    execute: config.execute,
    permissions: config.permissions,
    getLocations: config.locations,
    examples: config.examples,
  }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

export function isDefinedTool<T>(tool: Tool<T>): tool is DefinedTool<T> {
  return 'permissions' in tool
}

export function getToolPermissions<T>(tool: Tool<T>): ToolPermission[] {
  return isDefinedTool(tool) ? (tool.permissions ?? []) : []
}

export function getToolLocations<T>(tool: Tool<T>, input: T): ToolLocation[] | undefined {
  if (isDefinedTool(tool) && tool.getLocations) {
    return tool.getLocations(input)
  }
  return undefined
}
