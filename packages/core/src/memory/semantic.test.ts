/**
 * SemanticMemoryManager Tests
 *
 * Tests for fact-based semantic memory management.
 */

import { beforeEach, describe, expect, it } from 'vitest'
import { SemanticMemoryManager } from './semantic.js'
import { createTestDependencies } from './test-helpers.js'
import type { CreateSemanticMemoryInput } from './types.js'

describe('SemanticMemoryManager', () => {
  let manager: SemanticMemoryManager
  const { store, embedder } = createTestDependencies()

  beforeEach(() => {
    store.clear()
    manager = new SemanticMemoryManager(store, embedder)
  })

  describe('learn', () => {
    it('creates new fact', async () => {
      const input: CreateSemanticMemoryInput = {
        fact: 'React is a JavaScript library for building user interfaces',
        source: 'documentation',
        confidence: 0.9,
        tags: ['react', 'javascript'],
      }

      const id = await manager.learn(input)
      const memory = await manager.get(id)

      expect(memory).toBeTruthy()
      expect(memory?.fact).toBe('React is a JavaScript library for building user interfaces')
      expect(memory?.source).toBe('documentation')
      expect(memory?.confidence).toBe(0.9)
      expect(memory?.metadata.tags).toContain('react')
      expect(memory?.metadata.tags).toContain('javascript')
    })

    it('detects duplicates and reinforces existing (exact same text)', async () => {
      const exactText = 'TypeScript is a typed superset of JavaScript'
      const input: CreateSemanticMemoryInput = {
        fact: exactText,
        source: 'documentation',
        confidence: 0.8,
      }

      const id1 = await manager.learn(input)
      const memory1 = await manager.get(id1)
      const importance1 = memory1!.metadata.importance

      // Learn the exact same fact again (same text = same embedding)
      const id2 = await manager.learn(input)

      // Should return the same ID
      expect(id2).toBe(id1)

      // Importance should be boosted
      const memory2 = await manager.get(id1)
      expect(memory2!.metadata.importance).toBeGreaterThan(importance1)
    })

    it('updates confidence if new is higher (exact same text)', async () => {
      const exactText = 'Node.js is a JavaScript runtime'
      const input1: CreateSemanticMemoryInput = {
        fact: exactText,
        source: 'blog',
        confidence: 0.7,
      }

      const input2: CreateSemanticMemoryInput = {
        fact: exactText,
        source: 'official-docs',
        confidence: 0.95,
      }

      const id1 = await manager.learn(input1)
      await manager.learn(input2)

      const memory = await manager.get(id1)
      // Only metadata.confidence is updated, not the top-level confidence field
      expect(memory!.metadata.confidence).toBe(0.95)
    })

    it('generates embedding for fact', async () => {
      const input: CreateSemanticMemoryInput = {
        fact: 'Vue is a progressive JavaScript framework',
        source: 'documentation',
      }

      const id = await manager.learn(input)
      const memory = await manager.get(id)

      expect(memory?.embedding).toBeDefined()
      expect(memory?.embedding).toBeInstanceOf(Float32Array)
    })

    it('stores fact in content field', async () => {
      const input: CreateSemanticMemoryInput = {
        fact: 'Angular is a TypeScript-based framework',
        source: 'documentation',
      }

      const id = await manager.learn(input)
      const memory = await manager.get(id)

      expect(memory?.content).toBe('Angular is a TypeScript-based framework')
    })

    it('calculates importance based on confidence', async () => {
      const lowConfidence: CreateSemanticMemoryInput = {
        fact: 'Low confidence fact',
        source: 'test',
        confidence: 0.3,
      }

      const highConfidence: CreateSemanticMemoryInput = {
        fact: 'High confidence fact',
        source: 'test',
        confidence: 0.9,
      }

      const id1 = await manager.learn(lowConfidence)
      const id2 = await manager.learn(highConfidence)

      const memory1 = await manager.get(id1)
      const memory2 = await manager.get(id2)

      expect(memory2!.metadata.importance).toBeGreaterThan(memory1!.metadata.importance)
    })
  })

  describe('query', () => {
    it('returns similar facts', async () => {
      const exactText = 'Python is a high-level programming language'
      const id = await manager.learn({
        fact: exactText,
        source: 'documentation',
      })

      // Query with exact same text for high similarity
      const results = await manager.query(exactText, 5)

      expect(results.length).toBeGreaterThan(0)
      expect(results[0].memory.id).toBe(id)
      expect(results[0].similarity).toBeGreaterThan(0.9) // Very high for exact match
    })

    it('records access for retrieved memories', async () => {
      const id = await manager.learn({
        fact: 'Rust is a systems programming language',
        source: 'documentation',
      })

      const before = await manager.get(id)
      const beforeCount = before!.metadata.accessCount

      await manager.query('Rust programming', 5)

      const after = await manager.get(id)
      expect(after!.metadata.accessCount).toBeGreaterThan(beforeCount)
    })

    it('limits results', async () => {
      await manager.learn({ fact: 'Fact 1', source: 'test' })
      await manager.learn({ fact: 'Fact 2', source: 'test' })
      await manager.learn({ fact: 'Fact 3', source: 'test' })

      const results = await manager.query('Fact', 2)

      expect(results.length).toBeLessThanOrEqual(2)
    })
  })

  describe('get', () => {
    it('returns semantic memory by ID', async () => {
      const id = await manager.learn({
        fact: 'Test fact',
        source: 'test',
      })

      const memory = await manager.get(id)

      expect(memory).toBeTruthy()
      expect(memory?.id).toBe(id)
      expect(memory?.type).toBe('semantic')
    })

    it('returns null for non-existent ID', async () => {
      const memory = await manager.get('non-existent')
      expect(memory).toBeNull()
    })
  })

  describe('reinforce', () => {
    it('boosts importance', async () => {
      const id = await manager.learn({
        fact: 'Test fact',
        source: 'test',
      })

      const before = await manager.get(id)
      const beforeImportance = before!.metadata.importance

      await manager.reinforce(id)

      const after = await manager.get(id)
      expect(after!.metadata.importance).toBeGreaterThan(beforeImportance)
      expect(after!.metadata.importance).toBeCloseTo(beforeImportance + 0.1, 2)
    })

    it('increments access count', async () => {
      const id = await manager.learn({
        fact: 'Test fact',
        source: 'test',
      })

      const before = await manager.get(id)
      const beforeCount = before!.metadata.accessCount

      await manager.reinforce(id)

      const after = await manager.get(id)
      expect(after!.metadata.accessCount).toBe(beforeCount + 1)
    })

    it('caps importance at 1.0', async () => {
      const id = await manager.learn({
        fact: 'Test fact',
        source: 'test',
        confidence: 0.95,
      })

      // Reinforce multiple times
      for (let i = 0; i < 10; i++) {
        await manager.reinforce(id)
      }

      const memory = await manager.get(id)
      expect(memory!.metadata.importance).toBeLessThanOrEqual(1.0)
    })
  })

  describe('updateConfidence', () => {
    it('updates confidence in metadata', async () => {
      const id = await manager.learn({
        fact: 'Test fact',
        source: 'test',
        confidence: 0.5,
      })

      await manager.updateConfidence(id, 0.9)

      const memory = await manager.get(id)
      // updateConfidence only updates metadata.confidence
      expect(memory!.metadata.confidence).toBe(0.9)
    })

    it('clamps confidence between 0 and 1', async () => {
      const id = await manager.learn({
        fact: 'Test fact',
        source: 'test',
        confidence: 0.5,
      })

      await manager.updateConfidence(id, 1.5)
      const memory1 = await manager.get(id)
      expect(memory1!.metadata.confidence).toBe(1.0)

      await manager.updateConfidence(id, -0.5)
      const memory2 = await manager.get(id)
      expect(memory2!.metadata.confidence).toBe(0.0)
    })
  })

  describe('count', () => {
    it('returns semantic count', async () => {
      // Use different facts to ensure they are not merged as duplicates
      await manager.learn({ fact: 'Unique fact number one', source: 'test' })
      await manager.learn({ fact: 'Unique fact number two', source: 'test' })

      const count = await manager.count()

      expect(count).toBe(2)
    })
  })
})
