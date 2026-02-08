/**
 * ConsolidationEngine Tests
 *
 * Tests for memory consolidation (decay, merge, promote).
 */

import { beforeEach, describe, expect, it } from 'vitest'
import { ConsolidationEngine, DEFAULT_DECAY_RATE } from './consolidation.js'
import { createTestDependencies } from './test-helpers.js'
import type { MemoryEntry } from './types.js'
import { DEFAULT_IMPORTANCE, MIN_IMPORTANCE_THRESHOLD } from './types.js'

describe('ConsolidationEngine', () => {
  let engine: ConsolidationEngine
  const { store, embedder } = createTestDependencies()

  beforeEach(() => {
    store.clear()
    engine = new ConsolidationEngine(store)
  })

  describe('consolidate', () => {
    it('runs all phases and returns result', async () => {
      // Create some test memories
      const embedding = await embedder.embed('test')
      await store.insert({
        id: 'test-1',
        type: 'semantic',
        content: 'Test fact 1',
        embedding,
        metadata: {
          timestamp: Date.now(),
          importance: DEFAULT_IMPORTANCE,
          accessCount: 0,
          lastAccessed: Date.now(),
          tags: [],
        },
        fact: 'Test fact 1',
        source: 'test',
        confidence: 0.8,
      })

      const result = await engine.consolidate()

      expect(result).toHaveProperty('decayed')
      expect(result).toHaveProperty('merged')
      expect(result).toHaveProperty('promoted')
      expect(result).toHaveProperty('totalRemaining')
      expect(typeof result.decayed).toBe('number')
      expect(typeof result.merged).toBe('number')
      expect(typeof result.promoted).toBe('number')
      expect(typeof result.totalRemaining).toBe('number')
    })
  })

  describe('decayOldMemories', () => {
    it('reduces importance based on age', async () => {
      const oldTimestamp = Date.now() - 24 * 60 * 60 * 1000 // 24 hours ago
      const embedding = await embedder.embed('old memory')

      await store.insert({
        id: 'old-memory',
        type: 'semantic',
        content: 'Old fact',
        embedding,
        metadata: {
          timestamp: oldTimestamp,
          importance: DEFAULT_IMPORTANCE,
          accessCount: 0,
          lastAccessed: oldTimestamp,
          tags: [],
        },
        fact: 'Old fact',
        source: 'test',
        confidence: 0.8,
      })

      const before = await store.get('old-memory')
      const beforeImportance = before!.metadata.importance

      await engine.decayOldMemories()

      const after = await store.get('old-memory')
      expect(after!.metadata.importance).toBeLessThan(beforeImportance)
    })

    it('removes entries below MIN_IMPORTANCE_THRESHOLD', async () => {
      const veryOldTimestamp = Date.now() - 365 * 24 * 60 * 60 * 1000 // 1 year ago
      const embedding = await embedder.embed('very old memory')

      await store.insert({
        id: 'very-old-memory',
        type: 'semantic',
        content: 'Very old fact',
        embedding,
        metadata: {
          timestamp: veryOldTimestamp,
          importance: MIN_IMPORTANCE_THRESHOLD + 0.01,
          accessCount: 0,
          lastAccessed: veryOldTimestamp,
          tags: [],
        },
        fact: 'Very old fact',
        source: 'test',
        confidence: 0.8,
      })

      const removed = await engine.decayOldMemories()

      expect(removed).toBeGreaterThan(0)
      const memory = await store.get('very-old-memory')
      expect(memory).toBeNull()
    })

    it('preserves recently accessed memories', async () => {
      const oldTimestamp = Date.now() - 48 * 60 * 60 * 1000 // 48 hours ago
      const recentAccess = Date.now() - 1 * 60 * 60 * 1000 // 1 hour ago
      const embedding = await embedder.embed('accessed memory')

      await store.insert({
        id: 'accessed-memory',
        type: 'semantic',
        content: 'Recently accessed fact',
        embedding,
        metadata: {
          timestamp: oldTimestamp,
          importance: DEFAULT_IMPORTANCE,
          accessCount: 5,
          lastAccessed: recentAccess,
          tags: [],
        },
        fact: 'Recently accessed fact',
        source: 'test',
        confidence: 0.8,
      })

      const before = await store.get('accessed-memory')
      const beforeImportance = before!.metadata.importance

      await engine.decayOldMemories()

      const after = await store.get('accessed-memory')
      // Should still exist and have higher importance due to recent access
      expect(after).toBeTruthy()
      expect(after!.metadata.importance).toBeGreaterThan(beforeImportance * 0.8)
    })

    it('applies access count boost', async () => {
      const oldTimestamp = Date.now() - 24 * 60 * 60 * 1000 // 24 hours ago
      const embedding = await embedder.embed('popular memory')

      await store.insert({
        id: 'popular-memory',
        type: 'semantic',
        content: 'Popular fact',
        embedding,
        metadata: {
          timestamp: oldTimestamp,
          importance: DEFAULT_IMPORTANCE,
          accessCount: 10,
          lastAccessed: oldTimestamp,
          tags: [],
        },
        fact: 'Popular fact',
        source: 'test',
        confidence: 0.8,
      })

      const before = await store.get('popular-memory')
      const beforeImportance = before!.metadata.importance

      await engine.decayOldMemories()

      const after = await store.get('popular-memory')
      // High access count should provide a boost
      expect(after!.metadata.importance).toBeGreaterThan(beforeImportance * 0.5)
    })
  })

  describe('promoteActiveMemories', () => {
    it('boosts importance for high-access memories', async () => {
      const embedding = await embedder.embed('active memory')

      await store.insert({
        id: 'active-memory',
        type: 'semantic',
        content: 'Active fact',
        embedding,
        metadata: {
          timestamp: Date.now(),
          importance: DEFAULT_IMPORTANCE,
          accessCount: 10, // Above PROMOTION_ACCESS_THRESHOLD (5)
          lastAccessed: Date.now(),
          tags: [],
        },
        fact: 'Active fact',
        source: 'test',
        confidence: 0.8,
      })

      const before = await store.get('active-memory')
      const beforeImportance = before!.metadata.importance

      const promoted = await engine.promoteActiveMemories()

      expect(promoted).toBe(1)

      const after = await store.get('active-memory')
      expect(after!.metadata.importance).toBeGreaterThan(beforeImportance)
      expect(after!.metadata.accessCount).toBe(0) // Reset after promotion
    })

    it('does not promote memories with low access count', async () => {
      const embedding = await embedder.embed('inactive memory')

      await store.insert({
        id: 'inactive-memory',
        type: 'semantic',
        content: 'Inactive fact',
        embedding,
        metadata: {
          timestamp: Date.now(),
          importance: DEFAULT_IMPORTANCE,
          accessCount: 2, // Below threshold
          lastAccessed: Date.now(),
          tags: [],
        },
        fact: 'Inactive fact',
        source: 'test',
        confidence: 0.8,
      })

      const promoted = await engine.promoteActiveMemories()

      expect(promoted).toBe(0)
    })
  })

  describe('calculateDecayFactor', () => {
    it('returns correct decay factor', () => {
      const now = Date.now()
      const ageHours = 24

      const entry: MemoryEntry = {
        id: 'test',
        type: 'semantic',
        content: 'Test',
        metadata: {
          timestamp: now - ageHours * 60 * 60 * 1000,
          importance: DEFAULT_IMPORTANCE,
          accessCount: 0,
          lastAccessed: now,
          tags: [],
        },
      }

      const factor = engine.calculateDecayFactor(entry)

      // Should be e^(-DEFAULT_DECAY_RATE * 24)
      const expected = Math.exp(-DEFAULT_DECAY_RATE * ageHours)
      expect(factor).toBeCloseTo(expected, 5)
    })

    it('returns 1.0 for brand new memories', () => {
      const now = Date.now()

      const entry: MemoryEntry = {
        id: 'test',
        type: 'semantic',
        content: 'Test',
        metadata: {
          timestamp: now,
          importance: DEFAULT_IMPORTANCE,
          accessCount: 0,
          lastAccessed: now,
          tags: [],
        },
      }

      const factor = engine.calculateDecayFactor(entry)

      expect(factor).toBeCloseTo(1.0, 2)
    })
  })

  describe('estimateTimeToRemoval', () => {
    it('returns positive hours for memories above threshold', () => {
      const entry: MemoryEntry = {
        id: 'test',
        type: 'semantic',
        content: 'Test',
        metadata: {
          timestamp: Date.now(),
          importance: DEFAULT_IMPORTANCE,
          accessCount: 0,
          lastAccessed: Date.now(),
          tags: [],
        },
      }

      const hours = engine.estimateTimeToRemoval(entry)

      expect(hours).toBeGreaterThan(0)
      expect(typeof hours).toBe('number')
    })

    it('returns 0 for memories at or below threshold', () => {
      const entry: MemoryEntry = {
        id: 'test',
        type: 'semantic',
        content: 'Test',
        metadata: {
          timestamp: Date.now(),
          importance: MIN_IMPORTANCE_THRESHOLD,
          accessCount: 0,
          lastAccessed: Date.now(),
          tags: [],
        },
      }

      const hours = engine.estimateTimeToRemoval(entry)

      expect(hours).toBe(0)
    })
  })

  describe('setDecayRate and getDecayRate', () => {
    it('sets and gets decay rate', () => {
      engine.setDecayRate(0.005)

      expect(engine.getDecayRate()).toBe(0.005)
    })

    it('clamps decay rate between 0 and 1', () => {
      engine.setDecayRate(1.5)
      expect(engine.getDecayRate()).toBe(1.0)

      engine.setDecayRate(-0.5)
      expect(engine.getDecayRate()).toBe(0.0)
    })

    it('uses default decay rate if not specified', () => {
      expect(engine.getDecayRate()).toBe(DEFAULT_DECAY_RATE)
    })

    it('accepts custom decay rate in constructor', () => {
      const customEngine = new ConsolidationEngine(store, { decayRate: 0.002 })

      expect(customEngine.getDecayRate()).toBe(0.002)
    })
  })
})
