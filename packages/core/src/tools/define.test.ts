/**
 * Tool Definition Tests
 * Tests for defineTool, isDefinedTool, getToolPermissions, getToolLocations
 */

import { describe, expect, it } from 'vitest'
import { z } from 'zod'
import { defineTool, getToolLocations, getToolPermissions, isDefinedTool } from './define.js'
import type { ToolContext, ToolLocation } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

const TestSchema = z.object({
  path: z.string(),
  count: z.number().optional(),
})

const StrictSchema = z.object({
  name: z.string().min(1),
  value: z.number().min(0),
})

function createTestContext(sessionId = 'test'): ToolContext {
  return {
    sessionId,
    workingDirectory: '/tmp',
    signal: new AbortController().signal,
  }
}

function createBasicTool() {
  return defineTool({
    name: 'test_tool',
    description: 'A test tool for unit tests',
    schema: TestSchema,
    execute: async (input) => ({
      success: true,
      output: `Read ${input.path} (count: ${input.count ?? 'all'})`,
    }),
  })
}

function createFullTool() {
  return defineTool({
    name: 'full_tool',
    description: 'A fully configured test tool',
    schema: TestSchema,
    permissions: ['read', 'write'],
    locations: (input) => [{ path: input.path, type: 'read' as const }],
    examples: [
      { input: { path: '/test.ts' }, description: 'Read a file' },
      { input: { path: '/src', count: 10 }, description: 'Read with limit' },
    ],
    execute: async (input) => ({
      success: true,
      output: `Processed ${input.path}`,
    }),
  })
}

// ============================================================================
// defineTool - Definition
// ============================================================================

describe('defineTool', () => {
  describe('definition', () => {
    it('should create tool with correct name', () => {
      const tool = createBasicTool()
      expect(tool.definition.name).toBe('test_tool')
    })

    it('should create tool with correct description', () => {
      const tool = createBasicTool()
      expect(tool.definition.description).toBe('A test tool for unit tests')
    })

    it('should generate input_schema from Zod schema', () => {
      const tool = createBasicTool()
      expect(tool.definition.input_schema).toBeDefined()
      expect(tool.definition.input_schema.type).toBe('object')
    })

    it('should include properties in input_schema', () => {
      const tool = createBasicTool()
      const schema = tool.definition.input_schema
      expect(schema.properties).toBeDefined()
      expect(schema.properties!.path).toBeDefined()
      expect(schema.properties!.count).toBeDefined()
    })

    it('should include required fields in input_schema', () => {
      const tool = createBasicTool()
      const schema = tool.definition.input_schema
      expect(schema.required).toBeDefined()
      expect(schema.required).toContain('path')
    })

    it('should not include $schema in input_schema', () => {
      const tool = createBasicTool()
      expect('$schema' in tool.definition.input_schema).toBe(false)
    })
  })

  // ==========================================================================
  // defineTool - Validation
  // ==========================================================================

  describe('validate', () => {
    it('should succeed with valid input', () => {
      const tool = createBasicTool()
      const result = tool.validate!({ path: '/test.ts' })
      expect(result).toEqual({ path: '/test.ts' })
    })

    it('should succeed with optional fields', () => {
      const tool = createBasicTool()
      const result = tool.validate!({ path: '/test.ts', count: 5 })
      expect(result).toEqual({ path: '/test.ts', count: 5 })
    })

    it('should throw with invalid input', () => {
      const tool = createBasicTool()
      expect(() => tool.validate!({})).toThrow('Validation error')
    })

    it('should throw with wrong types', () => {
      const tool = createBasicTool()
      expect(() => tool.validate!({ path: 123 })).toThrow()
    })

    it('should throw with informative message', () => {
      const tool = defineTool({
        name: 'strict_tool',
        description: 'Strict validation',
        schema: StrictSchema,
        execute: async () => ({ success: true, output: '' }),
      })

      expect(() => tool.validate!({})).toThrow('Validation error')
    })
  })

  // ==========================================================================
  // defineTool - Execute
  // ==========================================================================

  describe('execute', () => {
    it('should return success for valid input', async () => {
      const tool = createBasicTool()
      const ctx = createTestContext()
      const result = await tool.execute({ path: '/test.ts' }, ctx)

      expect(result.success).toBe(true)
      expect(result.output).toBe('Read /test.ts (count: all)')
    })

    it('should return success with optional fields', async () => {
      const tool = createBasicTool()
      const ctx = createTestContext()
      const result = await tool.execute({ path: '/test.ts', count: 3 }, ctx)

      expect(result.success).toBe(true)
      expect(result.output).toBe('Read /test.ts (count: 3)')
    })

    it('should return validation error for invalid input', async () => {
      const tool = createBasicTool()
      const ctx = createTestContext()
      const result = await tool.execute({} as never, ctx)

      expect(result.success).toBe(false)
      expect(result.error).toBe('validation_error')
      expect(result.output).toContain('Validation error')
    })

    it('should return validation error for wrong types', async () => {
      const tool = createBasicTool()
      const ctx = createTestContext()
      const result = await tool.execute({ path: 42 } as never, ctx)

      expect(result.success).toBe(false)
      expect(result.error).toBe('validation_error')
    })

    it('should catch execution errors', async () => {
      const tool = defineTool({
        name: 'failing_tool',
        description: 'A tool that throws',
        schema: TestSchema,
        execute: async () => {
          throw new Error('Unexpected failure')
        },
      })

      const ctx = createTestContext()
      const result = await tool.execute({ path: '/test.ts' }, ctx)

      expect(result.success).toBe(false)
      expect(result.error).toBe('execution_error')
      expect(result.output).toContain('Unexpected failure')
    })

    it('should catch non-Error execution errors', async () => {
      const tool = defineTool({
        name: 'string_throw_tool',
        description: 'A tool that throws a string',
        schema: TestSchema,
        execute: async () => {
          throw 'string error'
        },
      })

      const ctx = createTestContext()
      const result = await tool.execute({ path: '/test.ts' }, ctx)

      expect(result.success).toBe(false)
      expect(result.output).toContain('string error')
    })

    it('should add locations from config.locations', async () => {
      const tool = createFullTool()
      const ctx = createTestContext()
      const result = await tool.execute({ path: '/src/app.ts' }, ctx)

      expect(result.success).toBe(true)
      expect(result.locations).toBeDefined()
      expect(result.locations).toHaveLength(1)
      expect(result.locations![0].path).toBe('/src/app.ts')
      expect(result.locations![0].type).toBe('read')
    })

    it('should not override existing locations from execute', async () => {
      const existingLocations: ToolLocation[] = [{ path: '/custom.ts', type: 'write' }]

      const tool = defineTool({
        name: 'location_tool',
        description: 'Tool with explicit locations',
        schema: TestSchema,
        locations: (input) => [{ path: input.path, type: 'read' as const }],
        execute: async () => ({
          success: true,
          output: 'done',
          locations: existingLocations,
        }),
      })

      const ctx = createTestContext()
      const result = await tool.execute({ path: '/test.ts' }, ctx)

      // Should keep the execute-provided locations, not override with config.locations
      expect(result.locations).toEqual(existingLocations)
    })
  })

  // ==========================================================================
  // defineTool - Metadata pass-through
  // ==========================================================================

  describe('metadata', () => {
    it('should pass through permissions', () => {
      const tool = createFullTool()
      expect(tool.permissions).toEqual(['read', 'write'])
    })

    it('should have undefined permissions when not specified', () => {
      const tool = createBasicTool()
      expect(tool.permissions).toBeUndefined()
    })

    it('should pass through empty permissions array', () => {
      const tool = defineTool({
        name: 'no_perm_tool',
        description: 'No permissions',
        schema: TestSchema,
        permissions: [],
        execute: async () => ({ success: true, output: '' }),
      })
      expect(tool.permissions).toEqual([])
    })

    it('should pass through examples', () => {
      const tool = createFullTool()
      expect(tool.examples).toBeDefined()
      expect(tool.examples).toHaveLength(2)
      expect(tool.examples![0].description).toBe('Read a file')
      expect(tool.examples![0].input).toEqual({ path: '/test.ts' })
    })

    it('should have undefined examples when not specified', () => {
      const tool = createBasicTool()
      expect(tool.examples).toBeUndefined()
    })

    it('should pass through getLocations function', () => {
      const tool = createFullTool()
      expect(tool.getLocations).toBeDefined()
      expect(typeof tool.getLocations).toBe('function')

      const locations = tool.getLocations!({ path: '/test.ts' })
      expect(locations).toHaveLength(1)
      expect(locations[0].path).toBe('/test.ts')
    })

    it('should have undefined getLocations when not specified', () => {
      const tool = createBasicTool()
      expect(tool.getLocations).toBeUndefined()
    })
  })
})

// ============================================================================
// isDefinedTool
// ============================================================================

describe('isDefinedTool', () => {
  it('should return true for tool created with defineTool (with permissions)', () => {
    const tool = createFullTool()
    expect(isDefinedTool(tool)).toBe(true)
  })

  it('should return true for tool with only permissions', () => {
    const tool = defineTool({
      name: 'perm_only',
      description: 'Only has permissions',
      schema: TestSchema,
      permissions: ['read'],
      execute: async () => ({ success: true, output: '' }),
    })
    expect(isDefinedTool(tool)).toBe(true)
  })

  it('should return true for tool with only getLocations', () => {
    const tool = defineTool({
      name: 'loc_only',
      description: 'Only has locations',
      schema: TestSchema,
      locations: () => [],
      execute: async () => ({ success: true, output: '' }),
    })
    expect(isDefinedTool(tool)).toBe(true)
  })

  it('should return true for tool with only examples', () => {
    const tool = defineTool({
      name: 'ex_only',
      description: 'Only has examples',
      schema: TestSchema,
      examples: [{ input: { path: '/test' }, description: 'test' }],
      execute: async () => ({ success: true, output: '' }),
    })
    expect(isDefinedTool(tool)).toBe(true)
  })

  it('should return false for plain tool object', () => {
    const plainTool = {
      definition: {
        name: 'plain',
        description: 'Plain tool',
        input_schema: { type: 'object' as const, properties: {} },
      },
      execute: async () => ({ success: true, output: '' }),
    }
    expect(isDefinedTool(plainTool)).toBe(false)
  })
})

// ============================================================================
// getToolPermissions
// ============================================================================

describe('getToolPermissions', () => {
  it('should return permissions from defined tool', () => {
    const tool = createFullTool()
    expect(getToolPermissions(tool)).toEqual(['read', 'write'])
  })

  it('should return empty array for tool without permissions property', () => {
    const plainTool = {
      definition: {
        name: 'plain',
        description: 'Plain tool',
        input_schema: { type: 'object' as const, properties: {} },
      },
      execute: async () => ({ success: true, output: '' }),
    }
    expect(getToolPermissions(plainTool)).toEqual([])
  })

  it('should return empty array for defined tool with undefined permissions', () => {
    const tool = createBasicTool()
    expect(getToolPermissions(tool)).toEqual([])
  })

  it('should return empty array for defined tool with empty permissions', () => {
    const tool = defineTool({
      name: 'empty_perm',
      description: 'Empty perms',
      schema: TestSchema,
      permissions: [],
      execute: async () => ({ success: true, output: '' }),
    })
    expect(getToolPermissions(tool)).toEqual([])
  })
})

// ============================================================================
// getToolLocations
// ============================================================================

describe('getToolLocations', () => {
  it('should return locations from defined tool', () => {
    const tool = createFullTool()
    const locations = getToolLocations(tool, { path: '/src/app.ts' })

    expect(locations).toBeDefined()
    expect(locations).toHaveLength(1)
    expect(locations![0].path).toBe('/src/app.ts')
    expect(locations![0].type).toBe('read')
  })

  it('should return undefined for tool without getLocations', () => {
    const tool = createBasicTool()
    const locations = getToolLocations(tool, { path: '/test.ts' })
    expect(locations).toBeUndefined()
  })

  it('should return undefined for plain tool', () => {
    const plainTool = {
      definition: {
        name: 'plain',
        description: 'Plain tool',
        input_schema: { type: 'object' as const, properties: {} },
      },
      execute: async () => ({ success: true, output: '' }),
    }
    const locations = getToolLocations(plainTool, {})
    expect(locations).toBeUndefined()
  })

  it('should return undefined when getLocations throws', () => {
    const tool = defineTool({
      name: 'throwing_loc',
      description: 'Throws in locations',
      schema: TestSchema,
      locations: () => {
        throw new Error('location error')
      },
      execute: async () => ({ success: true, output: '' }),
    })

    const locations = getToolLocations(tool, { path: '/test.ts' })
    expect(locations).toBeUndefined()
  })

  it('should pass input to getLocations function', () => {
    const tool = defineTool({
      name: 'dynamic_loc',
      description: 'Dynamic locations',
      schema: TestSchema,
      locations: (input) => {
        const locs: ToolLocation[] = [{ path: input.path, type: 'read' }]
        if (input.count && input.count > 5) {
          locs.push({ path: `${input.path}.bak`, type: 'write' })
        }
        return locs
      },
      execute: async () => ({ success: true, output: '' }),
    })

    const locsFew = getToolLocations(tool, { path: '/test.ts', count: 2 })
    expect(locsFew).toHaveLength(1)

    const locsMany = getToolLocations(tool, { path: '/test.ts', count: 10 })
    expect(locsMany).toHaveLength(2)
    expect(locsMany![1].path).toBe('/test.ts.bak')
  })
})
