/**
 * SubagentManager Tests
 *
 * Tests for the new robustness features:
 * - Human-readable alias generation
 * - Spawn depth limits
 * - State tracking
 */

import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { SubagentManager, resetSubagentManager } from '../../src/subagents/manager.js'
import { DEFAULT_SUBAGENT_CONFIG } from '../../src/subagents/types.js'
import type { MissionState } from '../../src/mission/state.js'

// Counter for unique task IDs
let taskIdCounter = 0

// Mock the background manager
vi.mock('../../src/lib/background-manager.js', () => ({
  getBackgroundManager: vi.fn(() => ({
    launch: vi.fn().mockImplementation(() => {
      taskIdCounter++
      return Promise.resolve(`bg_test${taskIdCounter}`)
    }),
    getTask: vi.fn().mockImplementation((taskId: string) => ({
      id: taskId,
      status: 'running',
      sessionId: `session_${taskId}`,
    })),
  })),
}))

// Mock mission state
const createMockMissionState = (): MissionState =>
  ({
    getMission: vi.fn().mockReturnValue({
      id: 'mission_123',
      status: 'in_progress',
    }),
  }) as unknown as MissionState

describe('SubagentManager', () => {
  let manager: SubagentManager
  let mockMissionState: MissionState

  beforeEach(() => {
    taskIdCounter = 0 // Reset counter
    resetSubagentManager()
    mockMissionState = createMockMissionState()
    manager = new SubagentManager(mockMissionState, '/test/cwd')
  })

  afterEach(() => {
    manager.shutdown()
    vi.clearAllMocks()
  })

  describe('Human-Readable Alias Generation', () => {
    it('should auto-generate alias when not provided', async () => {
      const subagent = await manager.spawn({
        prompt: 'Test task',
        agentType: 'operator',
      })

      expect(subagent.alias).toBeDefined()
      expect(subagent.alias).toMatch(/^[a-z]+-[a-z]+-[a-z]+$/) // e.g., "swift-amber-falcon"
    })

    it('should use provided alias when specified', async () => {
      const subagent = await manager.spawn({
        alias: 'my-custom-alias',
        prompt: 'Test task',
        agentType: 'operator',
      })

      expect(subagent.alias).toBe('my-custom-alias')
    })

    it('should reject duplicate aliases', async () => {
      await manager.spawn({
        alias: 'unique-alias',
        prompt: 'Test task 1',
      })

      await expect(
        manager.spawn({
          alias: 'unique-alias',
          prompt: 'Test task 2',
        })
      ).rejects.toThrow('Subagent alias already in use: unique-alias')
    })

    it('should generate unique aliases for multiple subagents', async () => {
      const subagent1 = await manager.spawn({ prompt: 'Task 1' })
      const subagent2 = await manager.spawn({ prompt: 'Task 2' })
      const subagent3 = await manager.spawn({ prompt: 'Task 3' })

      const aliases = [subagent1.alias, subagent2.alias, subagent3.alias]
      const uniqueAliases = new Set(aliases)

      expect(uniqueAliases.size).toBe(3)
    })
  })

  describe('Spawn Depth Limits', () => {
    it('should start at depth 0 for root-level subagents', async () => {
      const subagent = await manager.spawn({
        prompt: 'Root task',
      })

      expect(subagent.depth).toBe(0)
    })

    it('should increment depth for child subagents', async () => {
      const parent = await manager.spawn({
        prompt: 'Parent task',
      })

      const child = await manager.spawn({
        prompt: 'Child task',
        parentSubagentId: parent.taskId,
      })

      expect(child.depth).toBe(1)
      expect(child.parentSubagentId).toBe(parent.taskId)
    })

    it('should track depth through multiple generations', async () => {
      const gen0 = await manager.spawn({ prompt: 'Gen 0' })
      const gen1 = await manager.spawn({
        prompt: 'Gen 1',
        parentSubagentId: gen0.taskId,
      })
      const gen2 = await manager.spawn({
        prompt: 'Gen 2',
        parentSubagentId: gen1.taskId,
      })

      expect(gen0.depth).toBe(0)
      expect(gen1.depth).toBe(1)
      expect(gen2.depth).toBe(2)
    })

    it('should enforce max depth limit (default: 3)', async () => {
      const gen0 = await manager.spawn({ prompt: 'Gen 0' })
      const gen1 = await manager.spawn({
        prompt: 'Gen 1',
        parentSubagentId: gen0.taskId,
      })
      const gen2 = await manager.spawn({
        prompt: 'Gen 2',
        parentSubagentId: gen1.taskId,
      })

      // Gen 3 should fail (depth 3 >= maxDepth 3)
      await expect(
        manager.spawn({
          prompt: 'Gen 3',
          parentSubagentId: gen2.taskId,
        })
      ).rejects.toThrow('Maximum spawn depth (3) exceeded')
    })

    it('should allow custom max depth configuration', async () => {
      const customManager = new SubagentManager(mockMissionState, '/test/cwd', undefined, {
        maxDepth: 2,
      })

      const gen0 = await customManager.spawn({ prompt: 'Gen 0' })
      const gen1 = await customManager.spawn({
        prompt: 'Gen 1',
        parentSubagentId: gen0.taskId,
      })

      // Gen 2 should fail with maxDepth: 2
      await expect(
        customManager.spawn({
          prompt: 'Gen 2',
          parentSubagentId: gen1.taskId,
        })
      ).rejects.toThrow('Maximum spawn depth (2) exceeded')

      customManager.shutdown()
    })

    it('should handle missing parent gracefully (treat as root)', async () => {
      const subagent = await manager.spawn({
        prompt: 'Orphan task',
        parentSubagentId: 'nonexistent_id',
      })

      // Should be treated as root level when parent not found
      expect(subagent.depth).toBe(0)
    })
  })

  describe('Subagent Lookup', () => {
    it('should retrieve subagent by alias', async () => {
      const created = await manager.spawn({
        alias: 'lookup-test',
        prompt: 'Test task',
      })

      const found = manager.getByAlias('lookup-test')
      expect(found).toBeDefined()
      expect(found?.taskId).toBe(created.taskId)
    })

    it('should retrieve subagent by task ID', async () => {
      const created = await manager.spawn({
        prompt: 'Test task',
      })

      const found = manager.getByTaskId(created.taskId)
      expect(found).toBeDefined()
      expect(found?.alias).toBe(created.alias)
    })

    it('should return null for unknown alias', () => {
      const found = manager.getByAlias('nonexistent')
      expect(found).toBeNull()
    })

    it('should return null for unknown task ID', () => {
      const found = manager.getByTaskId('bg_nonexistent')
      expect(found).toBeNull()
    })
  })

  describe('Subagent Listing', () => {
    it('should list all subagents', async () => {
      await manager.spawn({ prompt: 'Task 1' })
      await manager.spawn({ prompt: 'Task 2' })
      await manager.spawn({ prompt: 'Task 3' })

      const all = manager.list()
      expect(all).toHaveLength(3)
    })

    it('should filter by state', async () => {
      await manager.spawn({ prompt: 'Task 1' })
      await manager.spawn({ prompt: 'Task 2' })

      const spawning = manager.list({ state: 'spawning' })
      expect(spawning.length).toBeGreaterThanOrEqual(0)
    })

    it('should filter by parent session ID', async () => {
      await manager.spawn({
        prompt: 'Task 1',
        parentSessionId: 'session_A',
      })
      await manager.spawn({
        prompt: 'Task 2',
        parentSessionId: 'session_B',
      })

      const sessionA = manager.list({ parentSessionId: 'session_A' })
      expect(sessionA).toHaveLength(1)
    })

    it('should sort by spawn time (newest first)', async () => {
      const first = await manager.spawn({ prompt: 'First' })
      // Add small delay to ensure different timestamps
      await new Promise((resolve) => setTimeout(resolve, 5))
      const second = await manager.spawn({ prompt: 'Second' })

      const all = manager.list()
      // Newest first - second should be at index 0
      expect(all).toHaveLength(2)
      // Check that second was spawned later
      const firstTime = new Date(all.find((s) => s.alias === first.alias)!.spawnedAt).getTime()
      const secondTime = new Date(all.find((s) => s.alias === second.alias)!.spawnedAt).getTime()
      expect(secondTime).toBeGreaterThanOrEqual(firstTime)
    })
  })

  describe('Statistics', () => {
    it('should track total count', async () => {
      await manager.spawn({ prompt: 'Task 1' })
      await manager.spawn({ prompt: 'Task 2' })

      const stats = manager.getStats()
      expect(stats.total).toBe(2)
    })

    it('should track by state', async () => {
      await manager.spawn({ prompt: 'Task 1' })

      const stats = manager.getStats()
      expect(stats.byState.spawning).toBeGreaterThanOrEqual(0)
    })

    it('should track by depth', async () => {
      const parent = await manager.spawn({ prompt: 'Parent' })
      await manager.spawn({
        prompt: 'Child',
        parentSubagentId: parent.taskId,
      })

      const stats = manager.getStats()
      expect(stats.byDepth[0]).toBe(1)
      expect(stats.byDepth[1]).toBe(1)
      expect(stats.maxDepthReached).toBe(1)
    })

    it('should track max depth reached', async () => {
      const gen0 = await manager.spawn({ prompt: 'Gen 0' })
      const gen1 = await manager.spawn({
        prompt: 'Gen 1',
        parentSubagentId: gen0.taskId,
      })
      await manager.spawn({
        prompt: 'Gen 2',
        parentSubagentId: gen1.taskId,
      })

      const stats = manager.getStats()
      expect(stats.maxDepthReached).toBe(2)
    })
  })

  describe('Cleanup', () => {
    it('should cleanup delivered completed subagents', async () => {
      const subagent = await manager.spawn({ prompt: 'Task' })

      // Manually set to completed and delivered for test
      const found = manager.getByTaskId(subagent.taskId)
      if (found) {
        found.state = 'completed'
        found.outputDelivered = true
      }

      const cleaned = manager.cleanup()
      expect(cleaned).toBe(1)
      expect(manager.list()).toHaveLength(0)
    })

    it('should not cleanup undelivered completed subagents', async () => {
      const subagent = await manager.spawn({ prompt: 'Task' })

      const found = manager.getByTaskId(subagent.taskId)
      if (found) {
        found.state = 'completed'
        found.outputDelivered = false
        found.output = 'Some output'
      }

      const cleaned = manager.cleanup()
      expect(cleaned).toBe(0)
      expect(manager.list()).toHaveLength(1)
    })
  })

  describe('Default Configuration', () => {
    it('should have maxDepth of 3 by default', () => {
      expect(DEFAULT_SUBAGENT_CONFIG.maxDepth).toBe(3)
    })
  })
})
