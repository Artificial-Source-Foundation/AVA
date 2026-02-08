/**
 * Conflict Detection Tests
 *
 * Tests for ConflictDetector and partitionTasks: reader-writer semantics,
 * multi-worker conflicts, release, and task partitioning.
 */

import { describe, expect, it } from 'vitest'
import type { BatchTask, WorkerDefinition, WorkerInputs } from '../types.js'
import { ConflictDetector, partitionTasks } from './conflict.js'

// ============================================================================
// Helpers
// ============================================================================

function makeTask(id: string, reads: string[] = [], writes: string[] = []): BatchTask {
  return {
    id,
    worker: {
      name: id,
      displayName: id,
      description: `Worker ${id}`,
      systemPrompt: '',
      tools: [],
    } as WorkerDefinition,
    inputs: { task: 'test', cwd: '/tmp' } as WorkerInputs,
    // DependentTask extension
    ...(reads.length > 0 || writes.length > 0 ? { expectedPaths: { reads, writes } } : {}),
  }
}

// ============================================================================
// ConflictDetector
// ============================================================================

describe('ConflictDetector', () => {
  // ==========================================================================
  // Basic Access
  // ==========================================================================

  describe('declareAccess', () => {
    it('allows first access (read)', () => {
      const detector = new ConflictDetector()
      const result = detector.declareAccess({
        path: '/src/main.ts',
        mode: 'read',
        workerId: 'w1',
      })

      expect(result.conflict).toBe(false)
    })

    it('allows first access (write)', () => {
      const detector = new ConflictDetector()
      const result = detector.declareAccess({
        path: '/src/main.ts',
        mode: 'write',
        workerId: 'w1',
      })

      expect(result.conflict).toBe(false)
    })

    it('allows multiple readers', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/src/main.ts', mode: 'read', workerId: 'w1' })
      const result = detector.declareAccess({
        path: '/src/main.ts',
        mode: 'read',
        workerId: 'w2',
      })

      expect(result.conflict).toBe(false)
    })

    it('detects read-write conflict', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/src/main.ts', mode: 'read', workerId: 'w1' })
      const result = detector.declareAccess({
        path: '/src/main.ts',
        mode: 'write',
        workerId: 'w2',
      })

      expect(result.conflict).toBe(true)
      if (result.conflict) {
        expect(result.blockedBy).toBe('w1')
        expect(result.path).toBe('/src/main.ts')
      }
    })

    it('detects write-read conflict', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/src/main.ts', mode: 'write', workerId: 'w1' })
      const result = detector.declareAccess({
        path: '/src/main.ts',
        mode: 'read',
        workerId: 'w2',
      })

      expect(result.conflict).toBe(true)
    })

    it('detects write-write conflict', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/src/main.ts', mode: 'write', workerId: 'w1' })
      const result = detector.declareAccess({
        path: '/src/main.ts',
        mode: 'write',
        workerId: 'w2',
      })

      expect(result.conflict).toBe(true)
    })

    it('allows same worker to access same file', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/src/main.ts', mode: 'write', workerId: 'w1' })
      const result = detector.declareAccess({
        path: '/src/main.ts',
        mode: 'write',
        workerId: 'w1',
      })

      expect(result.conflict).toBe(false)
    })

    it('different paths do not conflict', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/src/a.ts', mode: 'write', workerId: 'w1' })
      const result = detector.declareAccess({
        path: '/src/b.ts',
        mode: 'write',
        workerId: 'w2',
      })

      expect(result.conflict).toBe(false)
    })
  })

  // ==========================================================================
  // checkWorker
  // ==========================================================================

  describe('checkWorker', () => {
    it('checks multiple accesses at once', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/src/main.ts', mode: 'write', workerId: 'w1' })

      const results = detector.checkWorker('w2', [
        { path: '/src/other.ts', mode: 'read', workerId: 'w2' },
        { path: '/src/main.ts', mode: 'read', workerId: 'w2' }, // conflict
      ])

      expect(results).toHaveLength(2)
      expect(results[0]!.conflict).toBe(false)
      expect(results[1]!.conflict).toBe(true)
    })
  })

  // ==========================================================================
  // Release
  // ==========================================================================

  describe('release', () => {
    it('releases all claims for a worker', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/src/a.ts', mode: 'write', workerId: 'w1' })
      detector.declareAccess({ path: '/src/b.ts', mode: 'read', workerId: 'w1' })

      detector.release('w1')

      // Now w2 should be able to write to both
      const r1 = detector.declareAccess({ path: '/src/a.ts', mode: 'write', workerId: 'w2' })
      const r2 = detector.declareAccess({ path: '/src/b.ts', mode: 'write', workerId: 'w2' })

      expect(r1.conflict).toBe(false)
      expect(r2.conflict).toBe(false)
    })

    it('does not affect other workers', () => {
      const detector = new ConflictDetector()
      // Both read — no conflict, so both claims are added
      detector.declareAccess({ path: '/src/a.ts', mode: 'read', workerId: 'w1' })
      detector.declareAccess({ path: '/src/a.ts', mode: 'read', workerId: 'w2' })

      detector.release('w1')

      // w2's read claim is still there
      expect(detector.hasClaims('/src/a.ts')).toBe(true)
    })
  })

  // ==========================================================================
  // Clear & Queries
  // ==========================================================================

  describe('clear', () => {
    it('removes all claims', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/a.ts', mode: 'write', workerId: 'w1' })
      detector.declareAccess({ path: '/b.ts', mode: 'read', workerId: 'w2' })

      detector.clear()

      expect(detector.hasClaims('/a.ts')).toBe(false)
      expect(detector.hasClaims('/b.ts')).toBe(false)
    })
  })

  describe('hasClaims', () => {
    it('returns true when path has claims', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/a.ts', mode: 'read', workerId: 'w1' })

      expect(detector.hasClaims('/a.ts')).toBe(true)
      expect(detector.hasClaims('/b.ts')).toBe(false)
    })
  })

  describe('hasWriteClaims', () => {
    it('returns true for write claims', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/a.ts', mode: 'write', workerId: 'w1' })

      expect(detector.hasWriteClaims('/a.ts')).toBe(true)
    })

    it('returns false for read-only claims', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/a.ts', mode: 'read', workerId: 'w1' })

      expect(detector.hasWriteClaims('/a.ts')).toBe(false)
    })

    it('returns false for no claims', () => {
      const detector = new ConflictDetector()
      expect(detector.hasWriteClaims('/a.ts')).toBe(false)
    })
  })

  describe('getClaims', () => {
    it('returns a copy of claims', () => {
      const detector = new ConflictDetector()
      detector.declareAccess({ path: '/a.ts', mode: 'read', workerId: 'w1' })

      const claims1 = detector.getClaims()
      const claims2 = detector.getClaims()
      expect(claims1).not.toBe(claims2)
    })
  })
})

// ============================================================================
// partitionTasks
// ============================================================================

describe('partitionTasks', () => {
  it('puts tasks without expectedPaths in parallel', () => {
    const tasks = [makeTask('t1'), makeTask('t2'), makeTask('t3')]

    const { parallel, serialized, conflicts } = partitionTasks(tasks)

    expect(parallel).toHaveLength(3)
    expect(serialized).toHaveLength(0)
    expect(conflicts).toHaveLength(0)
  })

  it('puts non-conflicting tasks in parallel', () => {
    const tasks = [makeTask('t1', ['/a.ts'], ['/b.ts']), makeTask('t2', ['/c.ts'], ['/d.ts'])]

    const { parallel, serialized } = partitionTasks(tasks)

    expect(parallel).toHaveLength(2)
    expect(serialized).toHaveLength(0)
  })

  it('serializes tasks with write conflicts', () => {
    const tasks = [makeTask('t1', [], ['/shared.ts']), makeTask('t2', [], ['/shared.ts'])]

    const { parallel, serialized, conflicts } = partitionTasks(tasks)

    expect(parallel).toHaveLength(1)
    expect(serialized).toHaveLength(1)
    expect(conflicts).toHaveLength(1)
    expect(conflicts[0]!.path).toBe('/shared.ts')
    expect(conflicts[0]!.resolution).toBe('serialized')
  })

  it('serializes read-write conflicts', () => {
    const tasks = [makeTask('t1', ['/file.ts'], []), makeTask('t2', [], ['/file.ts'])]

    const { parallel, serialized } = partitionTasks(tasks)

    expect(parallel).toHaveLength(1)
    expect(serialized).toHaveLength(1)
  })

  it('allows multiple readers', () => {
    const tasks = [
      makeTask('t1', ['/file.ts'], []),
      makeTask('t2', ['/file.ts'], []),
      makeTask('t3', ['/file.ts'], []),
    ]

    const { parallel, serialized } = partitionTasks(tasks)

    expect(parallel).toHaveLength(3)
    expect(serialized).toHaveLength(0)
  })

  it('handles mixed declared and undeclared tasks', () => {
    const tasks = [
      makeTask('t1'), // no paths declared
      makeTask('t2', [], ['/file.ts']),
      makeTask('t3', [], ['/file.ts']), // conflicts with t2
    ]

    const { parallel, serialized } = partitionTasks(tasks)

    // t1 (no paths) and t2 go to parallel; t3 serialized
    expect(parallel).toHaveLength(2)
    expect(serialized).toHaveLength(1)
  })

  it('returns empty arrays for empty input', () => {
    const { parallel, serialized, conflicts } = partitionTasks([])

    expect(parallel).toEqual([])
    expect(serialized).toEqual([])
    expect(conflicts).toEqual([])
  })
})
