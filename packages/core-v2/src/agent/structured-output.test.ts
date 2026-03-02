/**
 * Tests for structured output support.
 */

import { describe, expect, it } from 'vitest'
import type { ToolContext } from '../tools/types.js'
import {
  buildStructuredOutputTool,
  buildStructuredOutputToolDefinition,
  STRUCTURED_OUTPUT_TOOL_NAME,
  validateStructuredOutput,
} from './structured-output.js'

// ─── Test Schema ────────────────────────────────────────────────────────────

const testSchema = {
  type: 'object',
  properties: {
    summary: { type: 'string', description: 'A brief summary' },
    confidence: { type: 'number', description: 'Confidence score 0-1' },
    tags: { type: 'array', description: 'List of tags' },
    metadata: { type: 'object', description: 'Additional metadata' },
    approved: { type: 'boolean', description: 'Whether approved' },
  },
  required: ['summary', 'confidence'],
}

// ─── validateStructuredOutput ───────────────────────────────────────────────

describe('validateStructuredOutput', () => {
  it('returns empty errors for valid output', () => {
    const output = { summary: 'Test', confidence: 0.9 }
    const errors = validateStructuredOutput(output, testSchema)
    expect(errors).toEqual([])
  })

  it('returns empty errors for valid output with all properties', () => {
    const output = {
      summary: 'Test',
      confidence: 0.9,
      tags: ['a', 'b'],
      metadata: { key: 'val' },
      approved: true,
    }
    const errors = validateStructuredOutput(output, testSchema)
    expect(errors).toEqual([])
  })

  it('reports missing required properties', () => {
    const output = { tags: ['a'] }
    const errors = validateStructuredOutput(output, testSchema)
    expect(errors).toContain('Missing required property: summary')
    expect(errors).toContain('Missing required property: confidence')
  })

  it('reports type mismatches', () => {
    const output = { summary: 123, confidence: 'high' }
    const errors = validateStructuredOutput(output, testSchema)
    expect(errors).toContain('Property "summary": expected string, got number')
    expect(errors).toContain('Property "confidence": expected number, got string')
  })

  it('reports error for null output', () => {
    const errors = validateStructuredOutput(null, testSchema)
    expect(errors).toContain('Output is null or undefined')
  })

  it('reports error for undefined output', () => {
    const errors = validateStructuredOutput(undefined, testSchema)
    expect(errors).toContain('Output is null or undefined')
  })

  it('reports error for array output', () => {
    const errors = validateStructuredOutput([], testSchema)
    expect(errors).toContain('Expected object, got array')
  })

  it('reports error for string output', () => {
    const errors = validateStructuredOutput('hello', testSchema)
    expect(errors).toContain('Expected object, got string')
  })

  it('skips type check for null property values', () => {
    const output = { summary: null, confidence: 0.5 }
    const errors = validateStructuredOutput(output, testSchema)
    expect(errors).toEqual([])
  })

  it('handles schema with no required field', () => {
    const schema = {
      type: 'object',
      properties: { name: { type: 'string' } },
    }
    const errors = validateStructuredOutput({}, schema)
    expect(errors).toEqual([])
  })

  it('handles schema with no properties field', () => {
    const schema = { type: 'object', required: ['id'] }
    const errors = validateStructuredOutput({}, schema)
    expect(errors).toContain('Missing required property: id')
  })

  it('detects array vs object type mismatch', () => {
    const output = { tags: 'not-an-array', confidence: 1, summary: 'ok' }
    const errors = validateStructuredOutput(output, testSchema)
    expect(errors).toContain('Property "tags": expected array, got string')
  })
})

// ─── buildStructuredOutputTool ──────────────────────────────────────────────

describe('buildStructuredOutputTool', () => {
  it('creates a tool with the correct name', () => {
    const tool = buildStructuredOutputTool(testSchema)
    expect(tool.definition.name).toBe(STRUCTURED_OUTPUT_TOOL_NAME)
    expect(tool.definition.name).toBe('__structured_output')
  })

  it('creates a tool definition matching the schema', () => {
    const tool = buildStructuredOutputTool(testSchema)
    expect(tool.definition.input_schema.type).toBe('object')
    expect(tool.definition.input_schema.properties).toHaveProperty('summary')
    expect(tool.definition.input_schema.properties).toHaveProperty('confidence')
    expect(tool.definition.input_schema.required).toEqual(['summary', 'confidence'])
  })

  it('has a description explaining usage', () => {
    const tool = buildStructuredOutputTool(testSchema)
    expect(tool.definition.description).toContain('structured response')
  })

  it('validate() passes for valid input', () => {
    const tool = buildStructuredOutputTool(testSchema)
    const input = { summary: 'Test result', confidence: 0.85 }
    expect(() => tool.validate!(input)).not.toThrow()
    expect(tool.validate!(input)).toEqual(input)
  })

  it('validate() throws for invalid input', () => {
    const tool = buildStructuredOutputTool(testSchema)
    expect(() => tool.validate!({})).toThrow('Structured output validation failed')
    expect(() => tool.validate!({})).toThrow('Missing required property: summary')
  })

  it('execute() returns JSON-serialized input', async () => {
    const tool = buildStructuredOutputTool(testSchema)
    const input = { summary: 'Result', confidence: 0.95 }
    const ctx = {
      sessionId: 'test',
      workingDirectory: '/tmp',
      signal: new AbortController().signal,
    } as ToolContext
    const result = await tool.execute(input, ctx)

    expect(result.success).toBe(true)
    expect(JSON.parse(result.output)).toEqual(input)
  })
})

// ─── buildStructuredOutputToolDefinition ─────────────────────────────────────

describe('buildStructuredOutputToolDefinition', () => {
  it('returns a ToolDefinition matching the schema', () => {
    const def = buildStructuredOutputToolDefinition(testSchema)
    expect(def.name).toBe(STRUCTURED_OUTPUT_TOOL_NAME)
    expect(def.input_schema.type).toBe('object')
    expect(def.input_schema.properties).toHaveProperty('summary')
    expect(def.input_schema.required).toEqual(['summary', 'confidence'])
  })
})
