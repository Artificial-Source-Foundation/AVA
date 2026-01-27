/**
 * Tests for Delta9 Multi-Mode Tool Dispatcher
 */

import { describe, it, expect, vi } from 'vitest'
import {
  MultiModeDispatcher,
  createMultiModeDispatcher,
  defineMode,
  executeMode,
  createModeRouter,
  validateMode,
  getModeEnum,
  type ModeDefinition,
} from '../../src/lib/multi-mode-tool.js'

describe('MultiModeDispatcher', () => {
  // Sample modes for testing
  const testModes = {
    add: {
      description: 'Add two numbers',
      params: { a: 0, b: 0 },
      handler: async (args: { a: number; b: number }) => args.a + args.b,
    },
    subtract: {
      description: 'Subtract two numbers',
      params: { a: 0, b: 0 },
      handler: async (args: { a: number; b: number }) => args.a - args.b,
    },
    multiply: {
      description: 'Multiply two numbers',
      params: { a: 0, b: 0 },
      handler: async (args: { a: number; b: number }) => args.a * args.b,
    },
  } satisfies Record<string, ModeDefinition>

  let dispatcher: MultiModeDispatcher<typeof testModes>

  beforeEach(() => {
    dispatcher = new MultiModeDispatcher({
      name: 'calculator',
      description: 'A simple calculator',
      modes: testModes,
    })
  })

  describe('basic operations', () => {
    it('returns tool name', () => {
      expect(dispatcher.getName()).toBe('calculator')
    })

    it('returns tool description', () => {
      expect(dispatcher.getDescription()).toBe('A simple calculator')
    })

    it('returns mode names', () => {
      const names = dispatcher.getModeNames()
      expect(names).toContain('add')
      expect(names).toContain('subtract')
      expect(names).toContain('multiply')
    })

    it('checks if mode exists', () => {
      expect(dispatcher.hasMode('add')).toBe(true)
      expect(dispatcher.hasMode('divide')).toBe(false)
    })

    it('gets mode definition', () => {
      const addMode = dispatcher.getMode('add')
      expect(addMode?.description).toBe('Add two numbers')
    })

    it('returns null for unknown mode', () => {
      expect(dispatcher.getMode('unknown')).toBeNull()
    })
  })

  describe('dispatch', () => {
    it('dispatches to correct mode handler', async () => {
      const result = await dispatcher.dispatch('add', { a: 5, b: 3 })

      expect(result.success).toBe(true)
      expect(result.mode).toBe('add')
      expect(result.result).toBe(8)
    })

    it('handles different modes', async () => {
      const addResult = await dispatcher.dispatch('add', { a: 10, b: 5 })
      const subResult = await dispatcher.dispatch('subtract', { a: 10, b: 5 })
      const mulResult = await dispatcher.dispatch('multiply', { a: 10, b: 5 })

      expect(addResult.result).toBe(15)
      expect(subResult.result).toBe(5)
      expect(mulResult.result).toBe(50)
    })

    it('returns error for unknown mode', async () => {
      const result = await dispatcher.dispatch('divide' as any, { a: 10, b: 5 })

      expect(result.success).toBe(false)
      expect(result.error).toContain('Unknown mode: divide')
      expect(result.error).toContain('Available:')
    })

    it('handles handler errors', async () => {
      const errorModes = {
        fail: {
          description: 'Always fails',
          params: {},
          handler: async () => {
            throw new Error('Test error')
          },
        },
      }

      const errorDispatcher = new MultiModeDispatcher({
        name: 'error-tool',
        description: 'Tool that errors',
        modes: errorModes,
      })

      const result = await errorDispatcher.dispatch('fail', {})

      expect(result.success).toBe(false)
      expect(result.error).toBe('Test error')
    })
  })

  describe('validation', () => {
    it('runs validation before handler', async () => {
      const validateFn = vi.fn((args: { value: number }) => {
        if (args.value < 0) return 'Value must be positive'
        return null
      })

      const validatedModes = {
        process: {
          description: 'Process a value',
          params: { value: 0 },
          handler: async (args: { value: number }) => args.value * 2,
          validate: validateFn,
        },
      }

      const validatedDispatcher = new MultiModeDispatcher({
        name: 'validated',
        description: 'Validated tool',
        modes: validatedModes,
      })

      // Valid input
      const valid = await validatedDispatcher.dispatch('process', { value: 5 })
      expect(valid.success).toBe(true)
      expect(valid.result).toBe(10)

      // Invalid input
      const invalid = await validatedDispatcher.dispatch('process', { value: -5 })
      expect(invalid.success).toBe(false)
      expect(invalid.error).toBe('Value must be positive')
    })
  })

  describe('buildCombinedDescription', () => {
    it('builds combined description with all modes', () => {
      const description = dispatcher.buildCombinedDescription()

      expect(description).toContain('A simple calculator')
      expect(description).toContain('Available modes:')
      expect(description).toContain('add: Add two numbers')
      expect(description).toContain('subtract: Subtract two numbers')
      expect(description).toContain('multiply: Multiply two numbers')
    })
  })

  describe('buildModeHelp', () => {
    it('builds help for specific mode', () => {
      const help = dispatcher.buildModeHelp('add')

      expect(help).toBe('calculator add: Add two numbers')
    })

    it('returns null for unknown mode', () => {
      const help = dispatcher.buildModeHelp('unknown')
      expect(help).toBeNull()
    })
  })

  describe('defaultMode', () => {
    it('returns default mode when set', () => {
      const withDefault = new MultiModeDispatcher({
        name: 'test',
        description: 'Test',
        modes: testModes,
        defaultMode: 'add',
      })

      expect(withDefault.getDefaultMode()).toBe('add')
    })

    it('returns null when no default', () => {
      expect(dispatcher.getDefaultMode()).toBeNull()
    })
  })
})

describe('factory functions', () => {
  describe('createMultiModeDispatcher', () => {
    it('creates dispatcher instance', () => {
      const dispatcher = createMultiModeDispatcher({
        name: 'test',
        description: 'Test tool',
        modes: {
          echo: {
            description: 'Echo input',
            params: {},
            handler: async (args: { text: string }) => args.text,
          },
        },
      })

      expect(dispatcher.getName()).toBe('test')
    })
  })

  describe('defineMode', () => {
    it('creates mode definition', () => {
      const mode = defineMode<{ value: number }, number>(
        'Double a number',
        async (args) => args.value * 2
      )

      expect(mode.description).toBe('Double a number')
    })

    it('creates mode with validation', () => {
      const mode = defineMode<{ value: number }, number>(
        'Positive only',
        async (args) => args.value,
        {
          validate: (args) => (args.value < 0 ? 'Must be positive' : null),
        }
      )

      expect(mode.validate?.({ value: -1 })).toBe('Must be positive')
      expect(mode.validate?.({ value: 1 })).toBeNull()
    })
  })
})

describe('utility functions', () => {
  const modes = {
    greet: {
      description: 'Say hello',
      params: {},
      handler: async (args: { name: string }) => `Hello, ${args.name}!`,
    },
  }

  const dispatcher = createMultiModeDispatcher({
    name: 'greeter',
    description: 'Greeting tool',
    modes,
  })

  describe('executeMode', () => {
    it('executes mode with success', async () => {
      const result = await executeMode<string>(dispatcher, 'greet', { name: 'World' })

      expect(result.success).toBe(true)
      expect(result.data).toBe('Hello, World!')
    })

    it('handles errors', async () => {
      const result = await executeMode(dispatcher, 'unknown', {})

      expect(result.success).toBe(false)
      expect(result.error).toContain('Unknown mode')
    })
  })

  describe('createModeRouter', () => {
    it('creates router function', async () => {
      const router = createModeRouter(dispatcher)
      const result = await router({ mode: 'greet', name: 'Alice' })

      const parsed = JSON.parse(result)
      expect(parsed.success).toBe(true)
      expect(parsed.result).toBe('Hello, Alice!')
    })

    it('returns error JSON for unknown mode', async () => {
      const router = createModeRouter(dispatcher)
      const result = await router({ mode: 'unknown' })

      const parsed = JSON.parse(result)
      expect(parsed.success).toBe(false)
      expect(parsed.error).toContain('Unknown mode')
    })
  })

  describe('validateMode', () => {
    it('returns null for valid mode', () => {
      const error = validateMode(dispatcher, 'greet')
      expect(error).toBeNull()
    })

    it('returns error for invalid mode', () => {
      const error = validateMode(dispatcher, 'invalid')
      expect(error).toContain('Unknown mode: invalid')
      expect(error).toContain('Available:')
    })
  })

  describe('getModeEnum', () => {
    it('returns frozen array of mode names', () => {
      const modeEnum = getModeEnum(dispatcher)

      expect(modeEnum).toContain('greet')
      expect(Object.isFrozen(modeEnum)).toBe(true)
    })
  })
})
