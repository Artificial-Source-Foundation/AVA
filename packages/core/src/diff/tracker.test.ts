/**
 * Diff Tracker Tests
 */

import { afterEach, describe, expect, it } from 'vitest'
import { DiffTracker, getDefaultTracker, resetDefaultTracker } from './tracker.js'
import type { DiffTrackerEvent } from './types.js'

afterEach(() => {
  resetDefaultTracker()
})

// ============================================================================
// Core Operations
// ============================================================================

describe('DiffTracker', () => {
  describe('add', () => {
    it('adds a pending edit with diff', () => {
      const tracker = new DiffTracker()
      const edit = tracker.add('file.ts', 'old\n', 'new\n')
      expect(edit.path).toBe('file.ts')
      expect(edit.status).toBe('pending')
      expect(edit.diff).toContain('-old')
      expect(edit.diff).toContain('+new')
      expect(edit.id).toBeTruthy()
    })

    it('returns applied edit when no changes', () => {
      const tracker = new DiffTracker()
      const edit = tracker.add('file.ts', 'same\n', 'same\n')
      expect(edit.status).toBe('applied')
      expect(edit.diff).toBe('')
    })

    it('uses custom ID when provided', () => {
      const tracker = new DiffTracker()
      const edit = tracker.add('file.ts', 'old\n', 'new\n', { id: 'custom-id' })
      expect(edit.id).toBe('custom-id')
    })

    it('stores description', () => {
      const tracker = new DiffTracker()
      const edit = tracker.add('file.ts', 'old\n', 'new\n', { description: 'Fix bug' })
      expect(edit.description).toBe('Fix bug')
    })
  })

  describe('apply', () => {
    it('marks edit as applied', () => {
      const tracker = new DiffTracker()
      const edit = tracker.add('file.ts', 'old\n', 'new\n')
      const result = tracker.apply(edit.id)
      expect(result?.status).toBe('applied')
      expect(result?.resolvedAt).toBeDefined()
    })

    it('returns undefined for unknown ID', () => {
      const tracker = new DiffTracker()
      expect(tracker.apply('nonexistent')).toBeUndefined()
    })

    it('returns undefined for already applied edit', () => {
      const tracker = new DiffTracker()
      const edit = tracker.add('file.ts', 'old\n', 'new\n')
      tracker.apply(edit.id)
      expect(tracker.apply(edit.id)).toBeUndefined()
    })
  })

  describe('reject', () => {
    it('marks edit as rejected', () => {
      const tracker = new DiffTracker()
      const edit = tracker.add('file.ts', 'old\n', 'new\n')
      const result = tracker.reject(edit.id)
      expect(result?.status).toBe('rejected')
      expect(result?.resolvedAt).toBeDefined()
    })

    it('returns undefined for unknown ID', () => {
      const tracker = new DiffTracker()
      expect(tracker.reject('nonexistent')).toBeUndefined()
    })
  })

  describe('get', () => {
    it('retrieves edit by ID', () => {
      const tracker = new DiffTracker()
      const edit = tracker.add('file.ts', 'old\n', 'new\n')
      expect(tracker.get(edit.id)).toBe(edit)
    })

    it('returns undefined for unknown ID', () => {
      const tracker = new DiffTracker()
      expect(tracker.get('nonexistent')).toBeUndefined()
    })
  })

  describe('delete', () => {
    it('removes edit from tracker', () => {
      const tracker = new DiffTracker()
      const edit = tracker.add('file.ts', 'old\n', 'new\n')
      expect(tracker.delete(edit.id)).toBe(true)
      expect(tracker.get(edit.id)).toBeUndefined()
    })

    it('returns false for unknown ID', () => {
      const tracker = new DiffTracker()
      expect(tracker.delete('nonexistent')).toBe(false)
    })

    it('removes from path index', () => {
      const tracker = new DiffTracker()
      const edit = tracker.add('file.ts', 'old\n', 'new\n')
      tracker.delete(edit.id)
      expect(tracker.getByPath('file.ts')).toHaveLength(0)
    })
  })
})

// ============================================================================
// Query Methods
// ============================================================================

describe('DiffTracker queries', () => {
  it('getPending returns only pending edits', () => {
    const tracker = new DiffTracker()
    const e1 = tracker.add('a.ts', 'old\n', 'new1\n')
    const e2 = tracker.add('b.ts', 'old\n', 'new2\n')
    tracker.apply(e1.id)
    expect(tracker.getPending()).toHaveLength(1)
    expect(tracker.getPending()[0].id).toBe(e2.id)
  })

  it('getByStatus filters by status', () => {
    const tracker = new DiffTracker()
    const e1 = tracker.add('a.ts', 'old\n', 'new1\n')
    tracker.add('b.ts', 'old\n', 'new2\n')
    tracker.apply(e1.id)
    expect(tracker.getByStatus('applied')).toHaveLength(1)
    expect(tracker.getByStatus('pending')).toHaveLength(1)
    expect(tracker.getByStatus('rejected')).toHaveLength(0)
  })

  it('getByPath returns edits for specific path', () => {
    const tracker = new DiffTracker()
    tracker.add('a.ts', 'old\n', 'new1\n')
    tracker.add('a.ts', 'old\n', 'new2\n')
    tracker.add('b.ts', 'old\n', 'new3\n')
    expect(tracker.getByPath('a.ts')).toHaveLength(2)
    expect(tracker.getByPath('b.ts')).toHaveLength(1)
    expect(tracker.getByPath('c.ts')).toHaveLength(0)
  })

  it('getAll returns all edits', () => {
    const tracker = new DiffTracker()
    tracker.add('a.ts', 'old\n', 'new1\n')
    tracker.add('b.ts', 'old\n', 'new2\n')
    expect(tracker.getAll()).toHaveLength(2)
  })

  it('hasPending detects pending edits', () => {
    const tracker = new DiffTracker()
    expect(tracker.hasPending()).toBe(false)
    tracker.add('a.ts', 'old\n', 'new\n')
    expect(tracker.hasPending()).toBe(true)
  })

  it('getCounts returns correct counts', () => {
    const tracker = new DiffTracker()
    const e1 = tracker.add('a.ts', 'old\n', 'new1\n')
    const e2 = tracker.add('b.ts', 'old\n', 'new2\n')
    tracker.add('c.ts', 'old\n', 'new3\n')
    tracker.apply(e1.id)
    tracker.reject(e2.id)
    const counts = tracker.getCounts()
    expect(counts.applied).toBe(1)
    expect(counts.rejected).toBe(1)
    expect(counts.pending).toBe(1)
  })

  it('getPendingStats aggregates diff stats', () => {
    const tracker = new DiffTracker()
    tracker.add('a.ts', 'line1\n', 'line1\nnewline\n')
    tracker.add('b.ts', 'old\n', 'new\n')
    const stats = tracker.getPendingStats()
    expect(stats.files).toBe(2)
    expect(stats.additions).toBeGreaterThanOrEqual(1)
  })
})

// ============================================================================
// Bulk Operations
// ============================================================================

describe('DiffTracker bulk operations', () => {
  it('applyAll applies all pending', () => {
    const tracker = new DiffTracker()
    tracker.add('a.ts', 'old\n', 'new1\n')
    tracker.add('b.ts', 'old\n', 'new2\n')
    const applied = tracker.applyAll()
    expect(applied).toHaveLength(2)
    expect(tracker.hasPending()).toBe(false)
  })

  it('rejectAll rejects all pending', () => {
    const tracker = new DiffTracker()
    tracker.add('a.ts', 'old\n', 'new1\n')
    tracker.add('b.ts', 'old\n', 'new2\n')
    const rejected = tracker.rejectAll()
    expect(rejected).toHaveLength(2)
    expect(tracker.hasPending()).toBe(false)
  })

  it('clear removes all edits', () => {
    const tracker = new DiffTracker()
    tracker.add('a.ts', 'old\n', 'new1\n')
    tracker.add('b.ts', 'old\n', 'new2\n')
    tracker.clear()
    expect(tracker.getAll()).toHaveLength(0)
  })

  it('clearResolved removes only non-pending', () => {
    const tracker = new DiffTracker()
    const e1 = tracker.add('a.ts', 'old\n', 'new1\n')
    tracker.add('b.ts', 'old\n', 'new2\n')
    tracker.apply(e1.id)
    const count = tracker.clearResolved()
    expect(count).toBe(1)
    expect(tracker.getAll()).toHaveLength(1)
  })
})

// ============================================================================
// Events
// ============================================================================

describe('DiffTracker events', () => {
  it('emits edit_added event', () => {
    const tracker = new DiffTracker()
    const events: DiffTrackerEvent[] = []
    tracker.subscribe((e) => events.push(e))
    tracker.add('file.ts', 'old\n', 'new\n')
    expect(events).toHaveLength(1)
    expect(events[0].type).toBe('edit_added')
  })

  it('emits edit_applied event', () => {
    const tracker = new DiffTracker()
    const events: DiffTrackerEvent[] = []
    tracker.subscribe((e) => events.push(e))
    const edit = tracker.add('file.ts', 'old\n', 'new\n')
    tracker.apply(edit.id)
    expect(events).toHaveLength(2)
    expect(events[1].type).toBe('edit_applied')
  })

  it('emits edit_rejected event', () => {
    const tracker = new DiffTracker()
    const events: DiffTrackerEvent[] = []
    tracker.subscribe((e) => events.push(e))
    const edit = tracker.add('file.ts', 'old\n', 'new\n')
    tracker.reject(edit.id)
    expect(events[1].type).toBe('edit_rejected')
  })

  it('emits edits_cleared event', () => {
    const tracker = new DiffTracker()
    const events: DiffTrackerEvent[] = []
    tracker.subscribe((e) => events.push(e))
    tracker.clear()
    expect(events[0].type).toBe('edits_cleared')
  })

  it('unsubscribe stops events', () => {
    const tracker = new DiffTracker()
    const events: DiffTrackerEvent[] = []
    const unsub = tracker.subscribe((e) => events.push(e))
    unsub()
    tracker.add('file.ts', 'old\n', 'new\n')
    expect(events).toHaveLength(0)
  })

  it('catches listener errors without breaking', () => {
    const tracker = new DiffTracker()
    tracker.subscribe(() => {
      throw new Error('listener error')
    })
    // Should not throw
    expect(() => tracker.add('file.ts', 'old\n', 'new\n')).not.toThrow()
  })
})

// ============================================================================
// Singleton
// ============================================================================

describe('default tracker singleton', () => {
  it('returns the same instance', () => {
    const a = getDefaultTracker()
    const b = getDefaultTracker()
    expect(a).toBe(b)
  })

  it('creates new instance after reset', () => {
    const a = getDefaultTracker()
    resetDefaultTracker()
    const b = getDefaultTracker()
    expect(a).not.toBe(b)
  })
})
