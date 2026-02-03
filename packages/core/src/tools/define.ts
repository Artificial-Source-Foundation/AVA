/**
 * Tool Definition Wrapper
 * Declarative tool definition with Zod validation
 *
 * Based on OpenCode's Tool.define() pattern
 */

import { z } from 'zod'
import type { Tool, ToolContext, ToolLocation, ToolResult } from './types.js'
import { formatZodError } from './validation.js'

// ============================================================================
// Types
// ============================================================================

/** Permission types for tools */
export type ToolPermission = 'read' | 'write' | 'delete' | 'execute'

/** Example for tool documentation */
export interface ToolExample<T> {
  input: T
  description: string
}

/** Any Zod schema type for generic constraints */
type AnyZodSchema = z.ZodType

/** Configuration for defining a tool */
export interface ToolConfig<TSchema extends AnyZodSchema> {
  /** Tool name (used in LLM tool calls) */
  name: string

  /** Description shown to the LLM */
  description: string

  /** Zod schema for input validation */
  schema: TSchema

  /** Tool execution function */
  execute: (input: z.infer<TSchema>, ctx: ToolContext) => Promise<ToolResult>

  /** Permission types this tool requires (for permission system) */
  permissions?: ToolPermission[]

  /** Function to get affected file paths from input (for permission system) */
  locations?: (input: z.infer<TSchema>) => ToolLocation[]

  /** Usage examples for documentation */
  examples?: ToolExample<z.infer<TSchema>>[]
}

/** Extended Tool interface with metadata from defineTool */
export interface DefinedTool<TInput = unknown> extends Tool<TInput> {
  /** Permission types this tool requires */
  permissions?: ToolPermission[]

  /** Get locations affected by this tool call */
  getLocations?: (input: TInput) => ToolLocation[]

  /** Usage examples */
  examples?: ToolExample<TInput>[]
}

// ============================================================================
// Main Function
// ============================================================================

/**
 * Define a tool with Zod schema validation
 *
 * @example
 * ```typescript
 * import { z } from 'zod'
 * import { defineTool } from './define.js'
 *
 * const ReadSchema = z.object({
 *   path: z.string().describe('File path to read'),
 *   offset: z.number().optional().describe('Line offset'),
 *   limit: z.number().optional().describe('Max lines to read'),
 * })
 *
 * export const readTool = defineTool({
 *   name: 'read_file',
 *   description: 'Read contents of a file',
 *   schema: ReadSchema,
 *   permissions: ['read'],
 *   locations: (input) => [{ path: input.path, type: 'read' }],
 *   execute: async (input, ctx) => {
 *     // input is fully typed as z.infer<typeof ReadSchema>
 *     const content = await readFile(input.path)
 *     return { success: true, output: content }
 *   },
 * })
 * ```
 */
export function defineTool<TSchema extends AnyZodSchema>(
  config: ToolConfig<TSchema>
): DefinedTool<z.infer<TSchema>> {
  // Convert Zod schema to JSON Schema for LLM using Zod 4's built-in function
  const rawSchema = z.toJSONSchema(config.schema, { unrepresentable: 'any' })

  // Remove $schema property if present (not needed for LLM tools)
  const jsonSchema = { ...rawSchema }
  if ('$schema' in jsonSchema) {
    delete (jsonSchema as Record<string, unknown>).$schema
  }

  const tool: DefinedTool<z.infer<TSchema>> = {
    definition: {
      name: config.name,
      description: config.description,
      input_schema: jsonSchema as {
        type: 'object'
        properties: Record<string, unknown>
        required?: string[]
      },
    },

    // Validation function using Zod
    validate: (rawInput: unknown): z.infer<TSchema> => {
      const result = config.schema.safeParse(rawInput)
      if (!result.success) {
        throw new Error(formatZodError(result.error))
      }
      return result.data
    },

    // Execute with validation
    execute: async (rawInput: z.infer<TSchema>, ctx: ToolContext): Promise<ToolResult> => {
      // Validate input
      const parseResult = config.schema.safeParse(rawInput)

      if (!parseResult.success) {
        return {
          success: false,
          output: formatZodError(parseResult.error),
          error: 'validation_error',
        }
      }

      const input = parseResult.data

      try {
        // Execute the tool
        const result = await config.execute(input, ctx)

        // Add location metadata if provided and not already set
        if (config.locations && !result.locations) {
          result.locations = config.locations(input)
        }

        return result
      } catch (err) {
        // Handle execution errors
        const message = err instanceof Error ? err.message : String(err)
        return {
          success: false,
          output: `Tool execution failed: ${message}`,
          error: 'execution_error',
        }
      }
    },

    // Expose metadata for permission system
    permissions: config.permissions,
    getLocations: config.locations,
    examples: config.examples,
  }

  return tool
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Check if a tool was created with defineTool (has extended properties)
 */
export function isDefinedTool<T>(tool: Tool<T>): tool is DefinedTool<T> {
  return 'permissions' in tool || 'getLocations' in tool || 'examples' in tool
}

/**
 * Get permissions required by a tool
 */
export function getToolPermissions<T>(tool: Tool<T>): ToolPermission[] {
  if (isDefinedTool(tool) && tool.permissions) {
    return tool.permissions
  }
  return []
}

/**
 * Get locations that will be affected by a tool call
 */
export function getToolLocations<T>(tool: Tool<T>, input: T): ToolLocation[] | undefined {
  if (isDefinedTool(tool) && tool.getLocations) {
    try {
      return tool.getLocations(input)
    } catch {
      return undefined
    }
  }
  return undefined
}
