/**
 * EpisodicMemoryManager Tests
 *
 * Tests for session-based episodic memory management.
 */

import { beforeEach, describe, expect, it } from 'vitest'
import { EpisodicMemoryManager } from './episodic.js'
import { createTestDependencies } from './test-helpers.js'
import type { CreateEpisodicMemoryInput } from './types.js'

describe('EpisodicMemoryManager', () => {
  let manager: EpisodicMemoryManager
  const { store, embedder } = createTestDependencies()

  beforeEach(() => {
    store.clear()
    manager = new EpisodicMemoryManager(store, embedder)
  })

  describe('recordSession', () => {
    it('creates memory with correct metadata', async () => {
      const input: CreateEpisodicMemoryInput = {
        sessionId: 'session-1',
        summary: 'Fixed a critical bug in the authentication system',
        decisions: ['Use JWT tokens', 'Add rate limiting'],
        toolsUsed: ['read', 'write', 'bash'],
        outcome: 'success',
        durationMinutes: 45,
      }

      const id = await manager.recordSession(input)
      const memory = await manager.get(id)

      expect(memory).toBeTruthy()
      expect(memory?.sessionId).toBe('session-1')
      expect(memory?.summary).toBe('Fixed a critical bug in the authentication system')
      expect(memory?.decisions).toEqual(['Use JWT tokens', 'Add rate limiting'])
      expect(memory?.toolsUsed).toEqual(['read', 'write', 'bash'])
      expect(memory?.outcome).toBe('success')
      expect(memory?.durationMinutes).toBe(45)
      expect(memory?.metadata.timestamp).toBeDefined()
      expect(memory?.metadata.accessCount).toBe(0)
      expect(memory?.metadata.tags).toBeDefined()
    })

    it('calculates importance with boost for successful outcomes', async () => {
      const successInput: CreateEpisodicMemoryInput = {
        sessionId: 'session-success',
        summary: 'Successful task',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      }

      const failureInput: CreateEpisodicMemoryInput = {
        sessionId: 'session-failure',
        summary: 'Failed task',
        decisions: [],
        toolsUsed: [],
        outcome: 'failure',
        durationMinutes: 10,
      }

      const successId = await manager.recordSession(successInput)
      const failureId = await manager.recordSession(failureInput)

      const successMemory = await manager.get(successId)
      const failureMemory = await manager.get(failureId)

      expect(successMemory!.metadata.importance).toBeGreaterThan(failureMemory!.metadata.importance)
    })

    it('calculates importance with boost for longer sessions', async () => {
      const shortInput: CreateEpisodicMemoryInput = {
        sessionId: 'session-short',
        summary: 'Short task',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 15,
      }

      const longInput: CreateEpisodicMemoryInput = {
        sessionId: 'session-long',
        summary: 'Long task',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 45,
      }

      const shortId = await manager.recordSession(shortInput)
      const longId = await manager.recordSession(longInput)

      const shortMemory = await manager.get(shortId)
      const longMemory = await manager.get(longId)

      expect(longMemory!.metadata.importance).toBeGreaterThan(shortMemory!.metadata.importance)
    })

    it('includes summary, decisions, tools, and outcome in content', async () => {
      const input: CreateEpisodicMemoryInput = {
        sessionId: 'session-1',
        summary: 'Implemented new feature',
        decisions: ['Use React hooks', 'Add tests'],
        toolsUsed: ['write', 'test'],
        outcome: 'success',
        durationMinutes: 30,
      }

      const id = await manager.recordSession(input)
      const memory = await manager.get(id)

      expect(memory?.content).toContain('Implemented new feature')
      expect(memory?.content).toContain('Use React hooks')
      expect(memory?.content).toContain('Add tests')
      expect(memory?.content).toContain('write')
      expect(memory?.content).toContain('test')
      expect(memory?.content).toContain('success')
    })

    it('generates embedding for content', async () => {
      const input: CreateEpisodicMemoryInput = {
        sessionId: 'session-1',
        summary: 'Test session',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      }

      const id = await manager.recordSession(input)
      const memory = await manager.get(id)

      expect(memory?.embedding).toBeDefined()
      expect(memory?.embedding).toBeInstanceOf(Float32Array)
    })

    it('adds tags including outcome and tool categories', async () => {
      const input: CreateEpisodicMemoryInput = {
        sessionId: 'session-1',
        summary: 'Test session',
        decisions: [],
        toolsUsed: ['read', 'write', 'bash'],
        outcome: 'success',
        durationMinutes: 10,
        tags: ['custom-tag'],
      }

      const id = await manager.recordSession(input)
      const memory = await manager.get(id)

      expect(memory?.metadata.tags).toContain('custom-tag')
      expect(memory?.metadata.tags).toContain('outcome:success')
      expect(memory?.metadata.tags).toContain('category:files')
      expect(memory?.metadata.tags).toContain('category:shell')
    })
  })

  describe('recallSimilar', () => {
    it('returns similar sessions', async () => {
      const summary = 'Fixed authentication bug using JWT tokens'
      const decisions = ['Use JWT']
      const toolsUsed = ['read', 'write']
      const outcome = 'success'

      const id = await manager.recordSession({
        sessionId: 'session-1',
        summary,
        decisions,
        toolsUsed,
        outcome,
        durationMinutes: 30,
      })

      // Build content the same way episodic manager does for high similarity
      const searchContent = [
        summary,
        `Decisions: ${decisions.join('; ')}`,
        `Tools: ${toolsUsed.join(', ')}`,
        `Outcome: ${outcome}`,
      ].join('\n')

      const results = await manager.recallSimilar(searchContent, 5)

      expect(results.length).toBeGreaterThan(0)
      expect(results[0].memory.id).toBe(id)
      // Should have very high similarity since we're querying with exact content
      expect(results[0].similarity).toBeGreaterThan(0.9)
    })

    it('limits results', async () => {
      await manager.recordSession({
        sessionId: 'session-1',
        summary: 'Task 1',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      })
      await manager.recordSession({
        sessionId: 'session-2',
        summary: 'Task 2',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      })
      await manager.recordSession({
        sessionId: 'session-3',
        summary: 'Task 3',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      })

      const results = await manager.recallSimilar('Task', 2)

      expect(results.length).toBeLessThanOrEqual(2)
    })
  })

  describe('get', () => {
    it('returns episodic memory by ID', async () => {
      const id = await manager.recordSession({
        sessionId: 'session-1',
        summary: 'Test session',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      })

      const memory = await manager.get(id)

      expect(memory).toBeTruthy()
      expect(memory?.id).toBe(id)
      expect(memory?.type).toBe('episodic')
    })

    it('returns null for non-existent ID', async () => {
      const memory = await manager.get('non-existent')
      expect(memory).toBeNull()
    })
  })

  describe('recordAccess', () => {
    it('increments access count', async () => {
      const id = await manager.recordSession({
        sessionId: 'session-1',
        summary: 'Test',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      })

      const before = await manager.get(id)
      const beforeCount = before!.metadata.accessCount

      await manager.recordAccess(id)

      const after = await manager.get(id)
      expect(after!.metadata.accessCount).toBe(beforeCount + 1)
    })

    it('updates last accessed timestamp', async () => {
      const id = await manager.recordSession({
        sessionId: 'session-1',
        summary: 'Test',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      })

      const before = await manager.get(id)
      const beforeAccessed = before!.metadata.lastAccessed

      // Wait a tiny bit to ensure timestamp changes
      await new Promise((resolve) => setTimeout(resolve, 5))

      await manager.recordAccess(id)

      const after = await manager.get(id)
      expect(after!.metadata.lastAccessed).toBeGreaterThan(beforeAccessed)
    })
  })

  describe('getRecent', () => {
    it('returns recent sessions ordered by time', async () => {
      const id1 = await manager.recordSession({
        sessionId: 'session-1',
        summary: 'First session',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      })

      // Wait to ensure different timestamps
      await new Promise((resolve) => setTimeout(resolve, 5))

      const id2 = await manager.recordSession({
        sessionId: 'session-2',
        summary: 'Second session',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      })

      const recent = await manager.getRecent(10)

      expect(recent.length).toBe(2)
      expect(recent[0].id).toBe(id2) // Most recent first
      expect(recent[1].id).toBe(id1)
    })

    it('limits results', async () => {
      await manager.recordSession({
        sessionId: 'session-1',
        summary: 'Session 1',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      })
      await manager.recordSession({
        sessionId: 'session-2',
        summary: 'Session 2',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      })

      const recent = await manager.getRecent(1)

      expect(recent.length).toBe(1)
    })
  })

  describe('count', () => {
    it('returns episodic count', async () => {
      await manager.recordSession({
        sessionId: 'session-1',
        summary: 'Session 1',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      })
      await manager.recordSession({
        sessionId: 'session-2',
        summary: 'Session 2',
        decisions: [],
        toolsUsed: [],
        outcome: 'success',
        durationMinutes: 10,
      })

      const count = await manager.count()

      expect(count).toBe(2)
    })
  })
})
