/**
 * MemoryManager Tests
 *
 * Tests for the unified memory manager that coordinates all subsystems.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { MemoryManager } from './manager.js'
import { createTestDependencies } from './test-helpers.js'
import type {
  CreateEpisodicMemoryInput,
  CreateProceduralMemoryInput,
  CreateSemanticMemoryInput,
} from './types.js'

describe('MemoryManager', () => {
  let manager: MemoryManager
  const { store, embedder } = createTestDependencies()

  beforeEach(() => {
    store.clear()
    manager = new MemoryManager({ store, embedder })
  })

  afterEach(() => {
    manager.dispose()
  })

  describe('constructor', () => {
    it('creates subsystems', () => {
      expect(manager.episodic).toBeDefined()
      expect(manager.semantic).toBeDefined()
      expect(manager.procedural).toBeDefined()
    })

    it('accepts custom store and embedder', () => {
      const customManager = new MemoryManager({ store, embedder })
      expect(customManager).toBeDefined()
      customManager.dispose()
    })
  })

  describe('remember', () => {
    it('routes episodic memories to episodic manager', async () => {
      const input: CreateEpisodicMemoryInput = {
        sessionId: 'session-1',
        summary: 'Test session',
        decisions: ['Use React'],
        toolsUsed: ['read', 'write'],
        outcome: 'success',
        durationMinutes: 30,
      }

      const id = await manager.remember(input, 'episodic')

      expect(id).toBeTruthy()
      const memory = await manager.episodic.get(id)
      expect(memory).toBeTruthy()
      expect(memory?.summary).toBe('Test session')
    })

    it('routes semantic memories to semantic manager', async () => {
      const input: CreateSemanticMemoryInput = {
        fact: 'TypeScript is a typed superset of JavaScript',
        source: 'documentation',
        confidence: 0.9,
      }

      const id = await manager.remember(input, 'semantic')

      expect(id).toBeTruthy()
      const memory = await manager.semantic.get(id)
      expect(memory).toBeTruthy()
      expect(memory?.fact).toBe('TypeScript is a typed superset of JavaScript')
    })

    it('routes procedural memories to procedural manager', async () => {
      const input: CreateProceduralMemoryInput = {
        context: 'Linting TypeScript files',
        action: 'Run eslint with --fix flag',
        tools: ['bash', 'eslint'],
        success: true,
      }

      const id = await manager.remember(input, 'procedural')

      expect(id).toBeTruthy()
      const memory = await manager.procedural.get(id)
      expect(memory).toBeTruthy()
      expect(memory?.action).toBe('Run eslint with --fix flag')
    })

    it('emits memory_created event', async () => {
      const listener = vi.fn()
      manager.on(listener)

      const input: CreateSemanticMemoryInput = {
        fact: 'Test fact',
        source: 'test',
      }

      const id = await manager.remember(input, 'semantic')

      expect(listener).toHaveBeenCalledWith({
        type: 'memory_created',
        id,
        memoryType: 'semantic',
      })
    })

    it('throws error for unknown memory type', async () => {
      const input: CreateSemanticMemoryInput = {
        fact: 'Test',
        source: 'test',
      }

      // biome-ignore lint/suspicious/noExplicitAny: testing invalid type intentionally
      await expect(manager.remember(input, 'unknown' as any)).rejects.toThrow(
        'Unknown memory type: unknown'
      )
    })
  })

  describe('recall', () => {
    it('delegates to store query', async () => {
      // Create some memories with different facts to avoid merging
      await manager.remember(
        {
          fact: 'Completely unique fact number one',
          source: 'test',
        },
        'semantic'
      )
      await manager.remember(
        {
          fact: 'Totally different fact number two',
          source: 'test',
        },
        'semantic'
      )

      const results = await manager.recall({ type: 'semantic' })

      expect(results).toHaveLength(2)
      expect(results.every((r) => r.type === 'semantic')).toBe(true)
    })

    it('filters by type', async () => {
      await manager.remember({ fact: 'Fact', source: 'test' }, 'semantic')
      await manager.remember(
        {
          context: 'Context',
          action: 'Action',
          tools: ['tool'],
          success: true,
        },
        'procedural'
      )

      const results = await manager.recall({ type: 'procedural' })

      expect(results).toHaveLength(1)
      expect(results[0].type).toBe('procedural')
    })
  })

  describe('recallSimilar', () => {
    it('uses embedder and store to find similar memories', async () => {
      const id = await manager.remember(
        {
          fact: 'React is a JavaScript library',
          source: 'test',
        },
        'semantic'
      )

      const results = await manager.recallSimilar('React is a JavaScript library', 5)

      expect(results.length).toBeGreaterThan(0)
      expect(results[0].memory.id).toBe(id)
      expect(results[0].similarity).toBeGreaterThan(0.9)
    })

    it('filters by type', async () => {
      await manager.remember({ fact: 'Semantic fact', source: 'test' }, 'semantic')
      await manager.remember(
        {
          context: 'Procedural context',
          action: 'Action',
          tools: ['tool'],
          success: true,
        },
        'procedural'
      )

      const results = await manager.recallSimilar('Procedural context', 5, 'procedural')

      expect(results).toHaveLength(1)
      expect(results[0].memory.type).toBe('procedural')
    })
  })

  describe('reinforce', () => {
    it('routes episodic memories to episodic manager', async () => {
      const id = await manager.remember(
        {
          sessionId: 'session-1',
          summary: 'Test',
          decisions: [],
          toolsUsed: [],
          outcome: 'success',
          durationMinutes: 10,
        },
        'episodic'
      )

      const before = await manager.episodic.get(id)
      const beforeCount = before!.metadata.accessCount

      await manager.reinforce(id)

      const after = await manager.episodic.get(id)
      expect(after!.metadata.accessCount).toBe(beforeCount + 1)
    })

    it('routes semantic memories to semantic manager', async () => {
      const id = await manager.remember(
        {
          fact: 'Test fact',
          source: 'test',
        },
        'semantic'
      )

      const before = await manager.semantic.get(id)
      const beforeImportance = before!.metadata.importance

      await manager.reinforce(id)

      const after = await manager.semantic.get(id)
      expect(after!.metadata.importance).toBeGreaterThan(beforeImportance)
    })

    it('routes procedural memories to procedural manager', async () => {
      const id = await manager.remember(
        {
          context: 'Context',
          action: 'Action',
          tools: ['tool'],
          success: true,
        },
        'procedural'
      )

      const before = await manager.procedural.get(id)
      const beforeCount = before!.useCount

      await manager.reinforce(id)

      const after = await manager.procedural.get(id)
      expect(after!.useCount).toBe(beforeCount + 1)
    })

    it('emits memory_reinforced event', async () => {
      const id = await manager.remember(
        {
          fact: 'Test',
          source: 'test',
        },
        'semantic'
      )

      const listener = vi.fn()
      manager.on(listener)

      await manager.reinforce(id)

      expect(listener).toHaveBeenCalledWith(
        expect.objectContaining({
          type: 'memory_reinforced',
          id,
        })
      )
    })

    it('handles non-existent memory gracefully', async () => {
      await expect(manager.reinforce('non-existent')).resolves.not.toThrow()
    })
  })

  describe('forget', () => {
    it('deletes memory from store', async () => {
      const id = await manager.remember(
        {
          fact: 'Test',
          source: 'test',
        },
        'semantic'
      )

      await manager.forget(id)

      const memory = await manager.get(id)
      expect(memory).toBeNull()
    })

    it('emits memory_deleted event', async () => {
      const id = await manager.remember(
        {
          fact: 'Test',
          source: 'test',
        },
        'semantic'
      )

      const listener = vi.fn()
      manager.on(listener)

      await manager.forget(id)

      expect(listener).toHaveBeenCalledWith({
        type: 'memory_deleted',
        id,
      })
    })
  })

  describe('consolidate', () => {
    it('delegates to consolidation engine', async () => {
      // Create some memories
      await manager.remember({ fact: 'Fact 1', source: 'test' }, 'semantic')
      await manager.remember({ fact: 'Fact 2', source: 'test' }, 'semantic')

      const result = await manager.consolidate()

      expect(result).toHaveProperty('decayed')
      expect(result).toHaveProperty('merged')
      expect(result).toHaveProperty('promoted')
      expect(result).toHaveProperty('totalRemaining')
    })

    it('emits consolidation_complete event', async () => {
      const listener = vi.fn()
      manager.on(listener)

      const result = await manager.consolidate()

      expect(listener).toHaveBeenCalledWith({
        type: 'consolidation_complete',
        result,
      })
    })
  })

  describe('get', () => {
    it('returns memory by ID', async () => {
      const id = await manager.remember(
        {
          fact: 'Test fact',
          source: 'test',
        },
        'semantic'
      )

      const memory = await manager.get(id)

      expect(memory).toBeTruthy()
      expect(memory?.id).toBe(id)
    })

    it('returns null for non-existent ID', async () => {
      const memory = await manager.get('non-existent')
      expect(memory).toBeNull()
    })
  })

  describe('count', () => {
    it('returns total count without type filter', async () => {
      await manager.remember({ fact: 'Unique semantic fact', source: 'test' }, 'semantic')
      await manager.remember(
        {
          context: 'Context',
          action: 'Action',
          tools: ['tool'],
          success: true,
        },
        'procedural'
      )

      const count = await manager.count()
      expect(count).toBe(2)
    })

    it('returns count filtered by type', async () => {
      await manager.remember({ fact: 'First unique semantic fact', source: 'test' }, 'semantic')
      await manager.remember({ fact: 'Second unique semantic fact', source: 'test' }, 'semantic')
      await manager.remember(
        {
          context: 'Context',
          action: 'Action',
          tools: ['tool'],
          success: true,
        },
        'procedural'
      )

      const count = await manager.count('semantic')
      expect(count).toBe(2)
    })
  })

  describe('getStats', () => {
    it('returns counts per type', async () => {
      await manager.remember(
        { fact: 'First completely unique semantic fact', source: 'test' },
        'semantic'
      )
      await manager.remember(
        { fact: 'Second completely unique semantic fact', source: 'test' },
        'semantic'
      )
      await manager.remember(
        {
          sessionId: 'session-1',
          summary: 'Test session summary',
          decisions: [],
          toolsUsed: [],
          outcome: 'success',
          durationMinutes: 10,
        },
        'episodic'
      )
      await manager.remember(
        {
          context: 'Unique procedural context',
          action: 'Action',
          tools: ['tool'],
          success: true,
        },
        'procedural'
      )

      const stats = await manager.getStats()

      expect(stats.total).toBe(4)
      expect(stats.semantic).toBe(2)
      expect(stats.episodic).toBe(1)
      expect(stats.procedural).toBe(1)
    })
  })

  describe('events', () => {
    it('allows subscribing to events', () => {
      const listener = vi.fn()
      const unsubscribe = manager.on(listener)

      expect(typeof unsubscribe).toBe('function')
    })

    it('allows unsubscribing from events', async () => {
      const listener = vi.fn()
      const unsubscribe = manager.on(listener)

      unsubscribe()

      await manager.remember({ fact: 'Test', source: 'test' }, 'semantic')

      expect(listener).not.toHaveBeenCalled()
    })

    it('handles listener errors gracefully', async () => {
      const errorListener = vi.fn(() => {
        throw new Error('Listener error')
      })
      const consoleWarn = vi.spyOn(console, 'warn').mockImplementation(() => {})

      manager.on(errorListener)

      await manager.remember({ fact: 'Test', source: 'test' }, 'semantic')

      expect(errorListener).toHaveBeenCalled()
      expect(consoleWarn).toHaveBeenCalled()

      consoleWarn.mockRestore()
    })
  })

  describe('dispose', () => {
    it('clears consolidation timer', () => {
      const timerManager = new MemoryManager({
        store,
        embedder,
        consolidationInterval: 1000,
      })

      timerManager.dispose()

      // No error should be thrown
      expect(true).toBe(true)
    })

    it('clears event listeners', async () => {
      const listener = vi.fn()
      manager.on(listener)

      manager.dispose()

      await manager.remember({ fact: 'Test', source: 'test' }, 'semantic')

      expect(listener).not.toHaveBeenCalled()
    })
  })
})
