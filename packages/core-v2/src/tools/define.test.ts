import { describe, expect, it } from 'vitest'
import * as z from 'zod'
import { defineTool, getToolLocations, getToolPermissions, isDefinedTool } from './define.js'
import type { ToolContext } from './types.js'

const testSchema = z.object({
  name: z.string(),
  count: z.number().optional(),
})

function makeCtx(): ToolContext {
  return {
    sessionId: 'test-session',
    workingDirectory: '/tmp',
    signal: new AbortController().signal,
  }
}

describe('defineTool', () => {
  it('creates tool with definition', () => {
    const tool = defineTool({
      name: 'test_tool',
      description: 'A test tool',
      schema: testSchema,
      async execute() {
        return { success: true, output: 'done' }
      },
    })

    expect(tool.definition.name).toBe('test_tool')
    expect(tool.definition.description).toBe('A test tool')
  })

  it('generates JSON schema from Zod', () => {
    const tool = defineTool({
      name: 'test_tool',
      description: 'A test tool',
      schema: testSchema,
      async execute() {
        return { success: true, output: 'done' }
      },
    })

    expect(tool.definition.input_schema.type).toBe('object')
    expect(tool.definition.input_schema.properties).toHaveProperty('name')
  })

  it('validates input with Zod', () => {
    const tool = defineTool({
      name: 'test_tool',
      description: 'Test',
      schema: testSchema,
      async execute() {
        return { success: true, output: 'done' }
      },
    })

    const result = tool.validate!({ name: 'hello', count: 5 })
    expect(result.name).toBe('hello')
    expect(result.count).toBe(5)
  })

  it('validate throws on invalid input', () => {
    const tool = defineTool({
      name: 'test_tool',
      description: 'Test',
      schema: testSchema,
      async execute() {
        return { success: true, output: 'done' }
      },
    })

    expect(() => tool.validate!({ count: 'not a number' })).toThrow()
  })

  it('uses custom validate when provided', () => {
    const tool = defineTool({
      name: 'test_tool',
      description: 'Test',
      schema: testSchema,
      validate(_params) {
        return { name: 'custom', count: 99 }
      },
      async execute() {
        return { success: true, output: 'done' }
      },
    })

    const result = tool.validate!({})
    expect(result.name).toBe('custom')
    expect(result.count).toBe(99)
  })

  it('executes handler', async () => {
    const tool = defineTool({
      name: 'test_tool',
      description: 'Test',
      schema: testSchema,
      async execute(input) {
        return { success: true, output: `Hello ${input.name}` }
      },
    })

    const result = await tool.execute({ name: 'world' }, makeCtx())
    expect(result.output).toBe('Hello world')
  })

  it('stores permissions', () => {
    const tool = defineTool({
      name: 'test_tool',
      description: 'Test',
      schema: testSchema,
      permissions: ['read', 'write'],
      async execute() {
        return { success: true, output: 'done' }
      },
    })

    expect(tool.permissions).toEqual(['read', 'write'])
  })

  it('stores location function', () => {
    const tool = defineTool({
      name: 'test_tool',
      description: 'Test',
      schema: testSchema,
      locations: (input) => [{ path: input.name, type: 'read' }],
      async execute() {
        return { success: true, output: 'done' }
      },
    })

    const locs = tool.getLocations!({ name: '/test.ts', count: undefined })
    expect(locs).toEqual([{ path: '/test.ts', type: 'read' }])
  })

  it('stores examples', () => {
    const tool = defineTool({
      name: 'test_tool',
      description: 'Test',
      schema: testSchema,
      examples: [{ input: { name: 'example' }, description: 'An example' }],
      async execute() {
        return { success: true, output: 'done' }
      },
    })

    expect(tool.examples).toHaveLength(1)
    expect(tool.examples![0].description).toBe('An example')
  })
})

describe('isDefinedTool', () => {
  it('returns true for defined tools', () => {
    const tool = defineTool({
      name: 'test',
      description: 'test',
      schema: testSchema,
      permissions: ['read'],
      async execute() {
        return { success: true, output: '' }
      },
    })
    expect(isDefinedTool(tool)).toBe(true)
  })

  it('returns false for plain tools', () => {
    const plainTool = {
      definition: {
        name: 'plain',
        description: 'plain',
        input_schema: { type: 'object' as const, properties: {} },
      },
      async execute() {
        return { success: true, output: '' }
      },
    }
    expect(isDefinedTool(plainTool)).toBe(false)
  })
})

describe('getToolPermissions', () => {
  it('returns permissions for defined tool', () => {
    const tool = defineTool({
      name: 'test',
      description: 'test',
      schema: testSchema,
      permissions: ['read', 'execute'],
      async execute() {
        return { success: true, output: '' }
      },
    })
    expect(getToolPermissions(tool)).toEqual(['read', 'execute'])
  })

  it('returns empty array for plain tool', () => {
    const plainTool = {
      definition: {
        name: 'plain',
        description: 'plain',
        input_schema: { type: 'object' as const, properties: {} },
      },
      async execute() {
        return { success: true, output: '' }
      },
    }
    expect(getToolPermissions(plainTool)).toEqual([])
  })
})

describe('getToolLocations', () => {
  it('returns locations for defined tool', () => {
    const tool = defineTool({
      name: 'test',
      description: 'test',
      schema: testSchema,
      locations: (input) => [{ path: input.name, type: 'read' }],
      async execute() {
        return { success: true, output: '' }
      },
    })
    const locs = getToolLocations(tool, { name: '/file.ts' })
    expect(locs).toEqual([{ path: '/file.ts', type: 'read' }])
  })

  it('returns undefined for tool without locations', () => {
    const tool = defineTool({
      name: 'test',
      description: 'test',
      schema: testSchema,
      async execute() {
        return { success: true, output: '' }
      },
    })
    expect(getToolLocations(tool, { name: 'x' })).toBeUndefined()
  })
})
