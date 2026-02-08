/**
 * Tool Wrapper Tests
 *
 * Tests for createWorkerTool, createAllWorkerTools, getDelegateToolNames,
 * and isDelegateToolFromRegistry — pure wrapping/naming functions.
 */

import { describe, expect, it } from 'vitest'
import { WorkerRegistry } from './registry.js'
import {
  createAllWorkerTools,
  createWorkerTool,
  getDelegateToolNames,
  isDelegateToolFromRegistry,
} from './tool-wrapper.js'
import type { WorkerDefinition } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

function makeWorker(name: string): WorkerDefinition {
  return {
    name,
    displayName: name.charAt(0).toUpperCase() + name.slice(1),
    description: `${name} worker for testing.`,
    systemPrompt: `You are the ${name} worker.`,
    tools: ['read', 'write'],
  }
}

// ============================================================================
// createWorkerTool
// ============================================================================

describe('createWorkerTool', () => {
  it('creates tool with delegate_ prefix', () => {
    const tool = createWorkerTool(makeWorker('coder'))

    expect(tool.definition.name).toBe('delegate_coder')
  })

  it('includes worker description in tool description', () => {
    const tool = createWorkerTool(makeWorker('coder'))

    expect(tool.definition.description).toContain('Coder')
    expect(tool.definition.description).toContain('coder worker for testing')
  })

  it('has task parameter (required) and context parameter (optional)', () => {
    const tool = createWorkerTool(makeWorker('coder'))
    const schema = tool.definition.input_schema

    expect(schema.properties.task).toBeDefined()
    expect(schema.properties.context).toBeDefined()
    expect(schema.required).toContain('task')
    expect(schema.required).not.toContain('context')
  })

  it('has executable function', () => {
    const tool = createWorkerTool(makeWorker('coder'))
    expect(typeof tool.execute).toBe('function')
  })

  it('includes available tools in description', () => {
    const tool = createWorkerTool(makeWorker('coder'))
    expect(tool.definition.description).toContain('read, write')
  })
})

// ============================================================================
// createAllWorkerTools
// ============================================================================

describe('createAllWorkerTools', () => {
  it('creates tools for all workers in registry', () => {
    const registry = new WorkerRegistry()
    registry.registerAll([makeWorker('coder'), makeWorker('tester'), makeWorker('reviewer')])

    const tools = createAllWorkerTools(registry)

    expect(tools).toHaveLength(3)
    const names = tools.map((t) => t.definition.name)
    expect(names).toContain('delegate_coder')
    expect(names).toContain('delegate_tester')
    expect(names).toContain('delegate_reviewer')
  })

  it('returns empty array for empty registry', () => {
    const registry = new WorkerRegistry()
    const tools = createAllWorkerTools(registry)
    expect(tools).toEqual([])
  })
})

// ============================================================================
// getDelegateToolNames
// ============================================================================

describe('getDelegateToolNames', () => {
  it('returns delegate_ prefixed names', () => {
    const registry = new WorkerRegistry()
    registry.registerAll([makeWorker('coder'), makeWorker('tester')])

    const names = getDelegateToolNames(registry)

    expect(names).toEqual(['delegate_coder', 'delegate_tester'])
  })

  it('returns empty for empty registry', () => {
    const registry = new WorkerRegistry()
    expect(getDelegateToolNames(registry)).toEqual([])
  })
})

// ============================================================================
// isDelegateToolFromRegistry
// ============================================================================

describe('isDelegateToolFromRegistry', () => {
  it('returns true for matching delegate tool', () => {
    const registry = new WorkerRegistry()
    registry.register(makeWorker('coder'))

    expect(isDelegateToolFromRegistry('delegate_coder', registry)).toBe(true)
  })

  it('returns false for non-delegate tool', () => {
    const registry = new WorkerRegistry()
    registry.register(makeWorker('coder'))

    expect(isDelegateToolFromRegistry('read', registry)).toBe(false)
  })

  it('returns false for delegate tool not in registry', () => {
    const registry = new WorkerRegistry()
    registry.register(makeWorker('coder'))

    expect(isDelegateToolFromRegistry('delegate_tester', registry)).toBe(false)
  })

  it('returns false for "delegate" without underscore', () => {
    const registry = new WorkerRegistry()
    expect(isDelegateToolFromRegistry('delegate', registry)).toBe(false)
  })
})
