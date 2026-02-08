/**
 * ProceduralMemoryManager Tests
 *
 * Tests for pattern-based procedural memory management.
 */

import { beforeEach, describe, expect, it } from 'vitest'
import { ProceduralMemoryManager } from './procedural.js'
import { createTestDependencies } from './test-helpers.js'
import type { CreateProceduralMemoryInput } from './types.js'
import { SUCCESS_RATE_THRESHOLD } from './types.js'

describe('ProceduralMemoryManager', () => {
  let manager: ProceduralMemoryManager
  const { store, embedder } = createTestDependencies()

  beforeEach(() => {
    store.clear()
    manager = new ProceduralMemoryManager(store, embedder)
  })

  describe('recordPattern', () => {
    it('creates new pattern', async () => {
      const input: CreateProceduralMemoryInput = {
        context: 'User needs to fix linting errors in TypeScript files',
        action: 'Run eslint with --fix flag',
        tools: ['bash', 'eslint'],
        success: true,
        tags: ['linting', 'typescript'],
      }

      const id = await manager.recordPattern(input)
      const memory = await manager.get(id)

      expect(memory).toBeTruthy()
      expect(memory?.context).toBe('User needs to fix linting errors in TypeScript files')
      expect(memory?.action).toBe('Run eslint with --fix flag')
      expect(memory?.tools).toEqual(['bash', 'eslint'])
      expect(memory?.useCount).toBe(1)
      expect(memory?.successCount).toBe(1)
      expect(memory?.successRate).toBe(1.0)
      expect(memory?.metadata.tags).toContain('linting')
      expect(memory?.metadata.tags).toContain('typescript')
    })

    it('updates existing pattern on similar context with same action', async () => {
      const input1: CreateProceduralMemoryInput = {
        context: 'Need to format code',
        action: 'Run prettier',
        tools: ['prettier'],
        success: true,
      }

      const input2: CreateProceduralMemoryInput = {
        context: 'Need to format code',
        action: 'Run prettier',
        tools: ['prettier'],
        success: true,
      }

      const id1 = await manager.recordPattern(input1)
      const id2 = await manager.recordPattern(input2)

      // Should update the same pattern
      expect(id2).toBe(id1)

      const memory = await manager.get(id1)
      expect(memory?.useCount).toBe(2)
      expect(memory?.successCount).toBe(2)
      expect(memory?.successRate).toBe(1.0)
    })

    it('creates new pattern if action is different', async () => {
      const input1: CreateProceduralMemoryInput = {
        context: 'Need to format code',
        action: 'Run prettier',
        tools: ['prettier'],
        success: true,
      }

      const input2: CreateProceduralMemoryInput = {
        context: 'Need to format code',
        action: 'Run biome format',
        tools: ['biome'],
        success: true,
      }

      const id1 = await manager.recordPattern(input1)
      const id2 = await manager.recordPattern(input2)

      // Should create different patterns
      expect(id2).not.toBe(id1)

      const count = await manager.count()
      expect(count).toBe(2)
    })

    it('generates embedding for context', async () => {
      const input: CreateProceduralMemoryInput = {
        context: 'Test context',
        action: 'Test action',
        tools: ['tool'],
        success: true,
      }

      const id = await manager.recordPattern(input)
      const memory = await manager.get(id)

      expect(memory?.embedding).toBeDefined()
      expect(memory?.embedding).toBeInstanceOf(Float32Array)
    })

    it('stores context and action in content', async () => {
      const input: CreateProceduralMemoryInput = {
        context: 'Fix bugs',
        action: 'Write tests',
        tools: ['vitest'],
        success: true,
      }

      const id = await manager.recordPattern(input)
      const memory = await manager.get(id)

      expect(memory?.content).toContain('Fix bugs')
      expect(memory?.content).toContain('Write tests')
    })

    it('adds tool tags', async () => {
      const input: CreateProceduralMemoryInput = {
        context: 'Test context',
        action: 'Test action',
        tools: ['bash', 'eslint'],
        success: true,
      }

      const id = await manager.recordPattern(input)
      const memory = await manager.get(id)

      expect(memory?.metadata.tags).toContain('tool:bash')
      expect(memory?.metadata.tags).toContain('tool:eslint')
    })
  })

  describe('suggestAction', () => {
    it('returns patterns above success threshold', async () => {
      // Create a successful pattern
      const id = await manager.recordPattern({
        context: 'Need to run tests',
        action: 'Use vitest',
        tools: ['vitest'],
        success: true,
      })

      // Reinforce it a few times
      await manager.recordOutcome(id, true)
      await manager.recordOutcome(id, true)

      const suggestions = await manager.suggestAction('run tests', 5)

      expect(suggestions.length).toBeGreaterThan(0)
      expect(suggestions[0].id).toBe(id)
      expect(suggestions[0].successRate).toBeGreaterThanOrEqual(SUCCESS_RATE_THRESHOLD)
    })

    it('filters out patterns below success threshold', async () => {
      // Create a pattern with low success rate
      const id = await manager.recordPattern({
        context: 'Deploy to production',
        action: 'Use manual deployment',
        tools: ['bash'],
        success: true,
      })

      // Record multiple failures
      await manager.recordOutcome(id, false)
      await manager.recordOutcome(id, false)
      await manager.recordOutcome(id, false)

      const suggestions = await manager.suggestAction('deploy production', 5)

      expect(suggestions).toHaveLength(0)
    })

    it('limits results', async () => {
      // Create multiple successful patterns
      await manager.recordPattern({
        context: 'Context 1',
        action: 'Action 1',
        tools: ['tool'],
        success: true,
      })
      await manager.recordPattern({
        context: 'Context 2',
        action: 'Action 2',
        tools: ['tool'],
        success: true,
      })
      await manager.recordPattern({
        context: 'Context 3',
        action: 'Action 3',
        tools: ['tool'],
        success: true,
      })

      const suggestions = await manager.suggestAction('Context', 2)

      expect(suggestions.length).toBeLessThanOrEqual(2)
    })

    it('records access for returned suggestions', async () => {
      const id = await manager.recordPattern({
        context: 'Test context',
        action: 'Test action',
        tools: ['tool'],
        success: true,
      })

      const before = await manager.get(id)
      const beforeCount = before!.metadata.accessCount

      await manager.suggestAction('Test context', 5)

      const after = await manager.get(id)
      expect(after!.metadata.accessCount).toBeGreaterThan(beforeCount)
    })
  })

  describe('get', () => {
    it('returns procedural memory by ID', async () => {
      const id = await manager.recordPattern({
        context: 'Test context',
        action: 'Test action',
        tools: ['tool'],
        success: true,
      })

      const memory = await manager.get(id)

      expect(memory).toBeTruthy()
      expect(memory?.id).toBe(id)
      expect(memory?.type).toBe('procedural')
    })

    it('returns null for non-existent ID', async () => {
      const memory = await manager.get('non-existent')
      expect(memory).toBeNull()
    })
  })

  describe('recordOutcome', () => {
    it('updates use count and success count on success', async () => {
      const id = await manager.recordPattern({
        context: 'Test context',
        action: 'Test action',
        tools: ['tool'],
        success: true,
      })

      const before = await manager.get(id)
      expect(before?.useCount).toBe(1)
      expect(before?.successCount).toBe(1)

      await manager.recordOutcome(id, true)

      const after = await manager.get(id)
      expect(after?.useCount).toBe(2)
      expect(after?.successCount).toBe(2)
      expect(after?.successRate).toBe(1.0)
    })

    it('updates use count but not success count on failure', async () => {
      const id = await manager.recordPattern({
        context: 'Test context',
        action: 'Test action',
        tools: ['tool'],
        success: true,
      })

      await manager.recordOutcome(id, false)

      const memory = await manager.get(id)
      expect(memory?.useCount).toBe(2)
      expect(memory?.successCount).toBe(1)
      expect(memory?.successRate).toBe(0.5)
    })

    it('increases importance on success', async () => {
      const id = await manager.recordPattern({
        context: 'Test context',
        action: 'Test action',
        tools: ['tool'],
        success: true,
      })

      const before = await manager.get(id)
      const beforeImportance = before!.metadata.importance

      await manager.recordOutcome(id, true)

      const after = await manager.get(id)
      expect(after!.metadata.importance).toBeGreaterThan(beforeImportance)
    })

    it('decreases importance on failure', async () => {
      const id = await manager.recordPattern({
        context: 'Test context',
        action: 'Test action',
        tools: ['tool'],
        success: true,
      })

      const before = await manager.get(id)
      const beforeImportance = before!.metadata.importance

      await manager.recordOutcome(id, false)

      const after = await manager.get(id)
      expect(after!.metadata.importance).toBeLessThan(beforeImportance)
    })
  })

  describe('count', () => {
    it('returns procedural count', async () => {
      await manager.recordPattern({
        context: 'Context 1',
        action: 'Action 1',
        tools: ['tool'],
        success: true,
      })
      await manager.recordPattern({
        context: 'Context 2',
        action: 'Action 2',
        tools: ['tool'],
        success: true,
      })

      const count = await manager.count()

      expect(count).toBe(2)
    })
  })
})
