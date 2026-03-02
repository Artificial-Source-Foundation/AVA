/**
 * Structured output support — JSON schema-validated agent responses.
 *
 * When `responseFormat` is set in AgentConfig, the agent is forced to call
 * an internal `__structured_output` tool whose input must match the given
 * JSON schema. This guarantees well-typed, validated output.
 */

import type { ToolDefinition } from '../llm/types.js'
import type { Tool, ToolContext, ToolResult } from '../tools/types.js'

// ─── Constants ──────────────────────────────────────────────────────────────

export const STRUCTURED_OUTPUT_TOOL_NAME = '__structured_output'

// ─── Schema Validation ──────────────────────────────────────────────────────

/**
 * Validate an output object against a JSON schema (basic validation).
 *
 * Checks:
 * - Output is a non-null object
 * - All required properties are present
 * - Property types match (string, number, boolean, array, object)
 *
 * Returns an array of error strings. Empty array means valid.
 */
export function validateStructuredOutput(
  output: unknown,
  schema: Record<string, unknown>
): string[] {
  const errors: string[] = []

  if (output === null || output === undefined) {
    errors.push('Output is null or undefined')
    return errors
  }

  if (typeof output !== 'object' || Array.isArray(output)) {
    errors.push(`Expected object, got ${Array.isArray(output) ? 'array' : typeof output}`)
    return errors
  }

  const obj = output as Record<string, unknown>
  const properties = (schema.properties ?? {}) as Record<string, Record<string, unknown>>
  const required = (schema.required ?? []) as string[]

  // Check required properties
  for (const key of required) {
    if (!(key in obj)) {
      errors.push(`Missing required property: ${key}`)
    }
  }

  // Check property types
  for (const [key, propSchema] of Object.entries(properties)) {
    if (!(key in obj)) continue
    const value = obj[key]
    const expectedType = propSchema.type as string | undefined

    if (expectedType && value !== null && value !== undefined) {
      const actualType = Array.isArray(value) ? 'array' : typeof value
      if (expectedType !== actualType) {
        errors.push(`Property "${key}": expected ${expectedType}, got ${actualType}`)
      }
    }
  }

  return errors
}

// ─── Tool Builder ───────────────────────────────────────────────────────────

/**
 * Build the internal `__structured_output` tool from a JSON schema.
 *
 * The tool validates the input against the schema and returns the
 * JSON-serialized result. The agent loop intercepts calls to this tool
 * to extract the structured response and finish the run.
 */
export function buildStructuredOutputTool(
  schema: Record<string, unknown>
): Tool<Record<string, unknown>> {
  const definition: ToolDefinition = {
    name: STRUCTURED_OUTPUT_TOOL_NAME,
    description:
      'Submit your final structured response. The input MUST be a JSON object ' +
      'matching the required schema. Do NOT wrap the output in any additional structure.',
    input_schema: {
      type: 'object',
      properties: (schema.properties ?? {}) as Record<string, unknown>,
      required: schema.required as string[] | undefined,
    },
  }

  return {
    definition,
    validate(params: unknown): Record<string, unknown> {
      const errors = validateStructuredOutput(params, schema)
      if (errors.length > 0) {
        throw new Error(`Structured output validation failed:\n${errors.join('\n')}`)
      }
      return params as Record<string, unknown>
    },
    async execute(params: Record<string, unknown>, _ctx: ToolContext): Promise<ToolResult> {
      return {
        success: true,
        output: JSON.stringify(params),
      }
    },
  }
}

/**
 * Build a ToolDefinition for the structured output tool (for passing to providers).
 */
export function buildStructuredOutputToolDefinition(
  schema: Record<string, unknown>
): ToolDefinition {
  return buildStructuredOutputTool(schema).definition
}
