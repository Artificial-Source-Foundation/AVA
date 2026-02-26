import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import * as z from 'zod'
import { installMockPlatform } from '../__test-utils__/mock-platform.js'
import { addToolMiddleware, resetRegistries } from '../extensions/api.js'
import { resetLogger } from '../logger/logger.js'
import { defineTool } from './define.js'
import {
  executeTool,
  getAllTools,
  getTool,
  getToolDefinitions,
  registerTool,
  resetTools,
  unregisterTool,
} from './registry.js'
import type { ToolContext } from './types.js'

const echoTool = defineTool({
  name: 'echo',
  description: 'Echo input',
  schema: z.object({ message: z.string() }),
  async execute(input) {
    return { success: true, output: input.message }
  },
})

const failTool = defineTool({
  name: 'fail',
  description: 'Always fails',
  schema: z.object({}),
  async execute() {
    throw new Error('intentional failure')
  },
})

function makeCtx(): ToolContext {
  return {
    sessionId: 'test',
    workingDirectory: '/tmp',
    signal: new AbortController().signal,
  }
}

describe('Tool Registry', () => {
  beforeEach(() => {
    resetTools()
    resetRegistries()
    installMockPlatform()
  })

  afterEach(() => {
    resetTools()
    resetRegistries()
    resetLogger()
  })

  // ─── Registration ─────────────────────────────────────────────────────

  describe('registration', () => {
    it('registers a tool', () => {
      registerTool(echoTool)
      expect(getTool('echo')).toBe(echoTool)
    })

    it('returns undefined for unregistered tool', () => {
      expect(getTool('nonexistent')).toBeUndefined()
    })

    it('unregisters a tool', () => {
      registerTool(echoTool)
      unregisterTool('echo')
      expect(getTool('echo')).toBeUndefined()
    })

    it('lists all tools', () => {
      registerTool(echoTool)
      registerTool(failTool)
      expect(getAllTools()).toHaveLength(2)
    })

    it('returns tool definitions', () => {
      registerTool(echoTool)
      const defs = getToolDefinitions()
      expect(defs).toHaveLength(1)
      expect(defs[0].name).toBe('echo')
    })

    it('clears all tools on reset', () => {
      registerTool(echoTool)
      resetTools()
      expect(getAllTools()).toHaveLength(0)
    })
  })

  // ─── Execution ────────────────────────────────────────────────────────

  describe('execution', () => {
    it('executes registered tool', async () => {
      registerTool(echoTool)
      const result = await executeTool('echo', { message: 'hello' }, makeCtx())
      expect(result.success).toBe(true)
      expect(result.output).toBe('hello')
    })

    it('returns error for unknown tool', async () => {
      const result = await executeTool('nonexistent', {}, makeCtx())
      expect(result.success).toBe(false)
      expect(result.error).toContain('Unknown tool')
    })

    it('catches execution errors', async () => {
      registerTool(failTool)
      const result = await executeTool('fail', {}, makeCtx())
      expect(result.success).toBe(false)
      expect(result.error).toContain('intentional failure')
    })

    it('validates input before execution', async () => {
      registerTool(echoTool)
      const result = await executeTool('echo', { message: 123 }, makeCtx())
      expect(result.success).toBe(false)
    })
  })

  // ─── Middleware ────────────────────────────────────────────────────────

  describe('middleware', () => {
    it('runs before-middleware', async () => {
      registerTool(echoTool)

      addToolMiddleware({
        name: 'test-before',
        priority: 0,
        async before(ctx) {
          return { args: { ...ctx.args, message: 'modified' } }
        },
      })

      const result = await executeTool('echo', { message: 'original' }, makeCtx())
      expect(result.output).toBe('modified')
    })

    it('blocks tool via before-middleware', async () => {
      registerTool(echoTool)

      addToolMiddleware({
        name: 'blocker',
        priority: 0,
        async before() {
          return { blocked: true, reason: 'not allowed' }
        },
      })

      const result = await executeTool('echo', { message: 'hello' }, makeCtx())
      expect(result.success).toBe(false)
      expect(result.error).toContain('not allowed')
    })

    it('runs after-middleware', async () => {
      registerTool(echoTool)

      addToolMiddleware({
        name: 'test-after',
        priority: 0,
        async after(_ctx, result) {
          return { result: { ...result, output: `wrapped: ${result.output}` } }
        },
      })

      const result = await executeTool('echo', { message: 'hello' }, makeCtx())
      expect(result.output).toBe('wrapped: hello')
    })

    it('runs middlewares in priority order', async () => {
      registerTool(echoTool)
      const order: string[] = []

      addToolMiddleware({
        name: 'second',
        priority: 10,
        async before() {
          order.push('second')
          return undefined
        },
      })

      addToolMiddleware({
        name: 'first',
        priority: 0,
        async before() {
          order.push('first')
          return undefined
        },
      })

      await executeTool('echo', { message: 'test' }, makeCtx())
      expect(order).toEqual(['first', 'second'])
    })
  })
})
