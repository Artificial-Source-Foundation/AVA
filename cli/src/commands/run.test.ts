/**
 * Tests for the run command
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'

// Mock platform before importing anything that uses it
vi.mock('@ava/platform-node', () => ({
  createNodePlatform: () => ({
    fs: {},
    shell: {},
    credentials: {
      get: vi.fn(async () => null),
      set: vi.fn(async () => {}),
      delete: vi.fn(async () => {}),
      has: vi.fn(async () => false),
    },
    database: {
      open: vi.fn(),
      close: vi.fn(),
    },
  }),
}))

describe('run command', () => {
  beforeEach(() => {
    vi.resetModules()
  })

  describe('MockLLMClient', () => {
    it('should yield a complete_task tool call', async () => {
      const { MockLLMClient } = await import('./mock-client.js')
      const client = new MockLLMClient()

      const messages = [{ role: 'user' as const, content: 'Test goal' }]
      const config = {
        provider: 'anthropic' as const,
        model: 'test',
        authMethod: 'api-key' as const,
      }

      const deltas: Array<{ content: string; toolUse?: { name: string } }> = []
      for await (const delta of client.stream(messages, config)) {
        deltas.push(delta)
      }

      expect(deltas.length).toBe(2)

      // First delta is the thought
      expect(deltas[0].content).toContain('Test goal')

      // Second delta has the complete_task tool call
      expect(deltas[1].toolUse).toBeDefined()
      expect(deltas[1].toolUse!.name).toBe('complete_task')
    })

    it('should extract goal from last user message', async () => {
      const { MockLLMClient } = await import('./mock-client.js')
      const client = new MockLLMClient()

      const messages = [
        { role: 'system' as const, content: 'You are helpful.' },
        { role: 'user' as const, content: 'First message' },
        { role: 'assistant' as const, content: 'OK' },
        { role: 'user' as const, content: 'Do the thing' },
      ]

      const deltas = []
      for await (const delta of client.stream(messages, {
        provider: 'anthropic',
        model: 'test',
        authMethod: 'api-key',
      })) {
        deltas.push(delta)
      }

      expect(deltas[0].content).toContain('Do the thing')
    })
  })

  describe('setupMockEnvironment', () => {
    it('should set AVA_ANTHROPIC_API_KEY env var', async () => {
      const original = process.env.AVA_ANTHROPIC_API_KEY

      const { setupMockEnvironment } = await import('./mock-client.js')
      setupMockEnvironment()

      expect(process.env.AVA_ANTHROPIC_API_KEY).toBe('mock-test-key')

      // Restore
      if (original === undefined) {
        delete process.env.AVA_ANTHROPIC_API_KEY
      } else {
        process.env.AVA_ANTHROPIC_API_KEY = original
      }
    })
  })
})
