/**
 * LEGION Mode Coordinator Tests
 */

import { describe, it, expect, beforeEach } from 'vitest'
import { LegionCoordinator, type LegionConfig } from '../../src/legion/index.js'

describe('LegionCoordinator', () => {
  let coordinator: LegionCoordinator
  const defaultConfig: LegionConfig = {
    enabled: true,
    maxOperators: 3,
    minTasksForLegion: 2,
    taskTimeout: 10000,
    autoResolveConflicts: true,
    mergeStrategy: 'smart',
    retryFailed: true,
    maxRetries: 2,
  }

  beforeEach(() => {
    coordinator = new LegionCoordinator(defaultConfig)
  })

  describe('configuration', () => {
    it('should initialize with provided config', () => {
      const config = coordinator.getConfig()
      expect(config.maxOperators).toBe(3)
      expect(config.minTasksForLegion).toBe(2)
    })

    it('should check if legion mode should be used', () => {
      expect(coordinator.shouldUseLegion(1)).toBe(false)
      expect(coordinator.shouldUseLegion(3)).toBe(true)
    })
  })

  describe('strike initialization', () => {
    it('should initialize a strike with tasks', async () => {
      const tasks = [
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 1',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 2',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
      ]

      const strike = await coordinator.initializeStrike('mission-1', tasks)

      expect(strike.id).toMatch(/^strike_/)
      expect(strike.missionId).toBe('mission-1')
      expect(strike.status).toBe('planning')
      expect(strike.tasks).toHaveLength(2)
      expect(strike.operators.length).toBeGreaterThanOrEqual(1)
    })

    it('should create distribution plan', async () => {
      const tasks = [
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 1',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'low' as const,
        },
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 2',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
      ]

      const strike = await coordinator.initializeStrike('mission-1', tasks)
      // createDistributionPlan takes the strike object, not strike ID
      const plan = coordinator.createDistributionPlan(strike, 'round_robin')

      expect(plan.strategy).toBe('round_robin')
      expect(plan.assignments).toHaveLength(2)
    })
  })

  describe('operator management', () => {
    it('should track operator status', async () => {
      const tasks = [
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 1',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
      ]

      const strike = await coordinator.initializeStrike('mission-1', tasks)
      const operators = strike.operators

      expect(operators.length).toBeGreaterThan(0)
      expect(operators[0].status).toBe('idle')
    })
  })

  describe('events', () => {
    it('should emit events', async () => {
      const events: unknown[] = []
      coordinator.onEvent((e) => events.push(e))

      const tasks = [
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 1',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
      ]

      await coordinator.initializeStrike('mission-1', tasks)

      expect(events.length).toBeGreaterThan(0)
      expect(events[0]).toHaveProperty('type')
    })

    it('should allow unsubscribing from events', async () => {
      const events: unknown[] = []
      const unsubscribe = coordinator.onEvent((e) => events.push(e))

      unsubscribe()

      const tasks = [
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 1',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
      ]

      await coordinator.initializeStrike('mission-1', tasks)

      expect(events).toHaveLength(0)
    })
  })

  describe('strikes', () => {
    it('should track active strikes', async () => {
      const tasks = [
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 1',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
      ]

      const strike = await coordinator.initializeStrike('mission-1', tasks)
      const activeStrikes = coordinator.getActiveStrikes()

      expect(activeStrikes).toHaveLength(1)
      expect(activeStrikes[0].id).toBe(strike.id)
    })

    it('should get strike by ID', async () => {
      const tasks = [
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 1',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
      ]

      const strike = await coordinator.initializeStrike('mission-1', tasks)
      const retrieved = coordinator.getStrike(strike.id)

      expect(retrieved).toBeDefined()
      expect(retrieved?.id).toBe(strike.id)
    })

    it('should return undefined for non-existent strike', () => {
      const strike = coordinator.getStrike('nonexistent')
      expect(strike).toBeUndefined()
    })
  })

  describe('conflict detection', () => {
    it('should detect file collisions', async () => {
      const tasks = [
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 1',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 2',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
      ]

      const strike = await coordinator.initializeStrike('mission-1', tasks)

      // Simulate both tasks being completed and modifying the same file
      strike.tasks[0].status = 'completed'
      strike.tasks[0].filesModified = ['src/index.ts']
      strike.tasks[1].status = 'completed'
      strike.tasks[1].filesModified = ['src/index.ts']

      const conflicts = coordinator.detectConflicts(strike)
      expect(conflicts.length).toBeGreaterThanOrEqual(1)
      expect(conflicts[0].conflictType).toBe('file_collision')
    })

    it('should not detect conflicts for different files', async () => {
      const tasks = [
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 1',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 2',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
      ]

      const strike = await coordinator.initializeStrike('mission-1', tasks)

      // Simulate both tasks being completed but modifying different files
      strike.tasks[0].status = 'completed'
      strike.tasks[0].filesModified = ['src/file1.ts']
      strike.tasks[1].status = 'completed'
      strike.tasks[1].filesModified = ['src/file2.ts']

      const conflicts = coordinator.detectConflicts(strike)
      expect(conflicts.length).toBe(0)
    })
  })

  describe('distribution strategies', () => {
    it('should support round_robin strategy', async () => {
      const tasks = [
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 1',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'low' as const,
        },
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 2',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'low' as const,
        },
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 3',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'low' as const,
        },
      ]

      const strike = await coordinator.initializeStrike('mission-1', tasks)
      const plan = coordinator.createDistributionPlan(strike, 'round_robin')

      expect(plan.strategy).toBe('round_robin')
      // Each task should be assigned
      expect(plan.assignments).toHaveLength(3)
    })

    it('should support load_balanced strategy', async () => {
      const tasks = [
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 1',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'medium' as const,
        },
      ]

      const strike = await coordinator.initializeStrike('mission-1', tasks)
      const plan = coordinator.createDistributionPlan(strike, 'load_balanced')

      expect(plan.strategy).toBe('load_balanced')
    })

    it('should support complexity_aware strategy', async () => {
      const tasks = [
        {
          missionId: 'mission-1',
          objectiveId: 'obj-1',
          description: 'Task 1',
          acceptanceCriteria: ['done'],
          dependencies: [],
          priority: 5,
          estimatedComplexity: 'high' as const,
        },
      ]

      const strike = await coordinator.initializeStrike('mission-1', tasks)
      const plan = coordinator.createDistributionPlan(strike, 'complexity_aware')

      expect(plan.strategy).toBe('complexity_aware')
    })
  })
})
