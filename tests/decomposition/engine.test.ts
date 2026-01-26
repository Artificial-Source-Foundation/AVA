/**
 * Delta9 Decomposition Engine Tests
 */

import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { join } from 'node:path'
import { rmSync, existsSync } from 'node:fs'
import {
  DecompositionEngine,
  getDecompositionEngine,
  resetDecompositionEngine,
  type Subtask,
  type DecompositionStrategy,
} from '../../src/decomposition/index.js'

// =============================================================================
// Test Helpers
// =============================================================================

const testBaseDir = join(process.cwd(), '.test-decomposition-' + Date.now())

function createTestSubtasks(): Subtask[] {
  return [
    {
      id: 'sub-1',
      title: 'Create user model',
      description: 'Add User schema with email and password fields',
      estimatedComplexity: 'low',
      files: ['src/models/user.ts'],
      acceptanceCriteria: ['User model exports correctly', 'Has password hashing'],
    },
    {
      id: 'sub-2',
      title: 'Create auth service',
      description: 'Add authentication service with login/logout methods',
      estimatedComplexity: 'medium',
      files: ['src/services/auth.ts'],
      dependencies: ['sub-1'],
      acceptanceCriteria: ['Login returns JWT', 'Logout invalidates token'],
    },
    {
      id: 'sub-3',
      title: 'Add auth endpoints',
      description: 'Create REST API endpoints for authentication',
      estimatedComplexity: 'medium',
      files: ['src/routes/auth.ts'],
      dependencies: ['sub-2'],
      acceptanceCriteria: ['POST /login works', 'POST /logout works', 'GET /me returns user'],
    },
  ]
}

// =============================================================================
// Engine Tests
// =============================================================================

describe('DecompositionEngine', () => {
  let engine: DecompositionEngine

  beforeEach(() => {
    resetDecompositionEngine()
    engine = new DecompositionEngine({ baseDir: testBaseDir })
  })

  afterEach(() => {
    engine.destroy()
    if (existsSync(testBaseDir)) {
      rmSync(testBaseDir, { recursive: true })
    }
  })

  describe('decompose()', () => {
    it('should create a decomposition with subtasks', () => {
      const subtasks = createTestSubtasks()
      const result = engine.decompose('task-123', 'Implement user authentication', {
        strategy: 'feature_based',
        subtasks,
      })

      expect(result.success).toBe(true)
      expect(result.decomposition).toBeDefined()
      expect(result.decomposition!.parentTaskId).toBe('task-123')
      expect(result.decomposition!.strategy).toBe('feature_based')
      expect(result.decomposition!.subtasks).toHaveLength(3)
      expect(result.decomposition!.totalEstimatedComplexity).toBe('medium')
    })

    it('should auto-select strategy if not provided', () => {
      const subtasks = createTestSubtasks()
      const result = engine.decompose('task-456', 'Refactor the legacy code incrementally', {
        subtasks,
      })

      expect(result.success).toBe(true)
      expect(result.decomposition!.strategy).toBe('incremental')
    })

    it('should validate decomposition and return quality score', () => {
      const subtasks = createTestSubtasks()
      const result = engine.decompose('task-789', 'Implement feature', {
        strategy: 'feature_based',
        subtasks,
      })

      expect(result.quality).toBeDefined()
      expect(result.quality!.score).toBeGreaterThan(0)
      expect(result.quality!.score).toBeLessThanOrEqual(1)
      expect(typeof result.quality!.passed).toBe('boolean')
    })

    it('should assign IDs and order to subtasks', () => {
      const subtasks: Subtask[] = [
        {
          id: '',
          title: 'Task 1',
          description: 'First task',
          estimatedComplexity: 'low',
          acceptanceCriteria: ['Done'],
        },
        {
          id: '',
          title: 'Task 2',
          description: 'Second task',
          estimatedComplexity: 'low',
          acceptanceCriteria: ['Complete'],
        },
      ]

      const result = engine.decompose('task-auto', 'Test auto IDs', {
        strategy: 'incremental',
        subtasks,
      })

      expect(result.decomposition!.subtasks[0].id).toBeTruthy()
      expect(result.decomposition!.subtasks[1].id).toBeTruthy()
      expect(result.decomposition!.subtasks[0].order).toBe(1)
      expect(result.decomposition!.subtasks[1].order).toBe(2)
    })
  })

  describe('selectStrategy()', () => {
    it('should select test_first for TDD tasks', () => {
      const strategy = engine.selectStrategy('Implement feature with test-driven development TDD')
      expect(strategy).toBe('test_first')
    })

    it('should select incremental for refactoring tasks', () => {
      const strategy = engine.selectStrategy('Refactor the payment module')
      expect(strategy).toBe('incremental')
    })

    it('should select layer_based for full-stack tasks', () => {
      const strategy = engine.selectStrategy('Build full stack user dashboard')
      expect(strategy).toBe('layer_based')
    })

    it('should select feature_based for feature tasks', () => {
      const strategy = engine.selectStrategy('Add new feature for notifications')
      expect(strategy).toBe('feature_based')
    })

    it('should select file_based when subtasks have files', () => {
      const subtasks = createTestSubtasks()
      const strategy = engine.selectStrategy('Generic task', subtasks)
      expect(strategy).toBe('file_based')
    })

    it('should default to feature_based', () => {
      const strategy = engine.selectStrategy('Do something')
      expect(strategy).toBe('feature_based')
    })
  })

  describe('redecompose()', () => {
    it('should create new decomposition with different strategy', () => {
      const subtasks = createTestSubtasks()
      const original = engine.decompose('task-redecompose', 'Original task', {
        strategy: 'feature_based',
        subtasks,
      })

      const result = engine.redecompose(original.decomposition!.id, 'layer_based')

      expect(result.success).toBe(true)
      expect(result.decomposition!.strategy).toBe('layer_based')
      expect(result.decomposition!.context?.previousDecompositionId).toBe(original.decomposition!.id)
    })

    it('should return error for non-existent decomposition', () => {
      const result = engine.redecompose('non-existent-id', 'layer_based')
      expect(result.success).toBe(false)
      expect(result.error).toContain('not found')
    })
  })

  describe('validate()', () => {
    it('should validate a decomposition', () => {
      const subtasks = createTestSubtasks()
      const decomposition = engine.decompose('task-validate', 'Test validation', {
        strategy: 'feature_based',
        subtasks,
      })

      const result = engine.validate(decomposition.decomposition!)

      expect(result.success).toBe(true)
      expect(result.quality).toBeDefined()
      expect(result.quality!.score).toBeGreaterThan(0)
    })
  })

  describe('recordOutcome()', () => {
    it('should record successful outcome', () => {
      const subtasks = createTestSubtasks()
      const decomposition = engine.decompose('task-outcome', 'Test outcome', {
        strategy: 'feature_based',
        subtasks,
      })

      const recorded = engine.recordOutcome(decomposition.decomposition!.id, true, 5000)

      expect(recorded).toBe(true)
    })

    it('should return false for non-existent decomposition', () => {
      const recorded = engine.recordOutcome('non-existent', true)
      expect(recorded).toBe(false)
    })
  })

  describe('searchSimilarTasks()', () => {
    it('should find similar tasks', () => {
      // Create some decompositions first
      const subtasks = createTestSubtasks()
      engine.decompose('task-auth-1', 'Implement user authentication with JWT tokens', {
        strategy: 'feature_based',
        subtasks,
      })
      engine.decompose('task-auth-2', 'Add OAuth user authentication login', {
        strategy: 'feature_based',
        subtasks,
      })

      // Search with enough overlapping words to exceed 0.3 Jaccard threshold
      const result = engine.searchSimilarTasks('Implement user authentication login', 5)

      expect(result.success).toBe(true)
      expect(result.similar.length).toBeGreaterThan(0)
      expect(result.similar[0].similarity).toBeGreaterThan(0)
    })

    it('should return empty when no history', () => {
      const result = engine.searchSimilarTasks('completely unique task', 5)
      expect(result.success).toBe(true)
      expect(result.similar).toHaveLength(0)
    })
  })

  describe('getStrategies()', () => {
    it('should return all strategies with descriptions', () => {
      const strategies = engine.getStrategies()

      expect(strategies).toHaveLength(5)
      expect(strategies.map(s => s.strategy)).toContain('file_based')
      expect(strategies.map(s => s.strategy)).toContain('feature_based')
      expect(strategies.map(s => s.strategy)).toContain('layer_based')
      expect(strategies.map(s => s.strategy)).toContain('test_first')
      expect(strategies.map(s => s.strategy)).toContain('incremental')
      strategies.forEach(s => {
        expect(s.description).toBeTruthy()
      })
    })
  })

  describe('getStats()', () => {
    it('should return statistics', () => {
      const subtasks = createTestSubtasks()
      engine.decompose('task-stats-1', 'First task', {
        strategy: 'feature_based',
        subtasks,
      })
      engine.decompose('task-stats-2', 'Second task', {
        strategy: 'layer_based',
        subtasks,
      })

      const stats = engine.getStats()

      expect(stats.totalDecompositions).toBe(2)
      expect(stats.byStrategy['feature_based']).toBe(1)
      expect(stats.byStrategy['layer_based']).toBe(1)
      expect(stats.averageSubtaskCount).toBe(3)
    })
  })

  describe('events', () => {
    it('should emit created event', () => {
      const events: any[] = []
      engine.on(event => events.push(event))

      const subtasks = createTestSubtasks()
      engine.decompose('task-event', 'Test events', {
        strategy: 'feature_based',
        subtasks,
      })

      expect(events.length).toBeGreaterThan(0)
      expect(events[0].type).toBe('created')
      expect(events[0].parentTaskId).toBe('task-event')
    })

    it('should emit validated event', () => {
      const events: any[] = []
      engine.on(event => events.push(event))

      const subtasks = createTestSubtasks()
      const decomposition = engine.decompose('task-validate-event', 'Test validate event', {
        strategy: 'feature_based',
        subtasks,
      })
      engine.validate(decomposition.decomposition!)

      const validatedEvent = events.find(e => e.type === 'validated')
      expect(validatedEvent).toBeDefined()
    })

    it('should allow removing event listeners', () => {
      const events: any[] = []
      const listener = (event: any) => events.push(event)
      engine.on(listener)
      engine.off(listener)

      const subtasks = createTestSubtasks()
      engine.decompose('task-no-event', 'Test no event', {
        strategy: 'feature_based',
        subtasks,
      })

      expect(events).toHaveLength(0)
    })
  })
})

// =============================================================================
// Singleton Tests
// =============================================================================

describe('Singleton Pattern', () => {
  afterEach(() => {
    resetDecompositionEngine()
  })

  it('should return same instance', () => {
    const engine1 = getDecompositionEngine()
    const engine2 = getDecompositionEngine()
    expect(engine1).toBe(engine2)
  })

  it('should reset instance', () => {
    const engine1 = getDecompositionEngine()
    resetDecompositionEngine()
    const engine2 = getDecompositionEngine()
    expect(engine1).not.toBe(engine2)
  })
})
