/**
 * Tests for Delta9 Event Store
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { existsSync, mkdirSync, rmSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { tmpdir } from 'node:os'
import {
  EventStore,
  getEventStore,
  clearEventStore,
  historyToVersionedEvent,
  importHistoryEvents,
  type VersionedEvent,
} from '../../src/lib/event-store.js'

describe('EventStore', () => {
  const testDir = join(tmpdir(), 'delta9-event-store-test-' + Date.now())
  let store: EventStore

  beforeEach(() => {
    // Create test directory
    mkdirSync(testDir, { recursive: true })
    mkdirSync(join(testDir, '.delta9'), { recursive: true })
    mkdirSync(join(testDir, '.delta9', 'snapshots'), { recursive: true })

    store = new EventStore({
      cwd: testDir,
      snapshotInterval: 5, // Small interval for testing
    })
  })

  afterEach(() => {
    store.clearCache()
    // Clean up test directory
    if (existsSync(testDir)) {
      rmSync(testDir, { recursive: true, force: true })
    }
    clearEventStore()
  })

  describe('append', () => {
    it('appends event to event log', () => {
      const event = store.append('mission-1', 'task_created', { taskId: 'task-1' })

      expect(event.version).toBe(1)
      expect(event.type).toBe('task_created')
      expect(event.aggregateId).toBe('mission-1')
      expect(event.sequence).toBe(1)
      expect(event.payload).toEqual({ taskId: 'task-1' })
      expect(event.timestamp).toBeDefined()
    })

    it('increments sequence number', () => {
      const e1 = store.append('mission-1', 'event-1', {})
      const e2 = store.append('mission-1', 'event-2', {})
      const e3 = store.append('mission-1', 'event-3', {})

      expect(e1.sequence).toBe(1)
      expect(e2.sequence).toBe(2)
      expect(e3.sequence).toBe(3)
    })

    it('tracks sequence per aggregate', () => {
      const m1e1 = store.append('mission-1', 'event', {})
      const m2e1 = store.append('mission-2', 'event', {})
      const m1e2 = store.append('mission-1', 'event', {})

      expect(m1e1.sequence).toBe(1)
      expect(m2e1.sequence).toBe(1)
      expect(m1e2.sequence).toBe(2)
    })

    it('persists events to file', () => {
      store.append('mission-1', 'test', { data: 'value' })

      const eventLogPath = join(testDir, '.delta9', 'events.jsonl')
      expect(existsSync(eventLogPath)).toBe(true)

      const content = readFileSync(eventLogPath, 'utf-8')
      const parsed = JSON.parse(content.trim())
      expect(parsed.type).toBe('test')
      expect(parsed.payload).toEqual({ data: 'value' })
    })
  })

  describe('appendBatch', () => {
    it('appends multiple events', () => {
      const events = store.appendBatch([
        { aggregateId: 'mission-1', type: 'event-1', payload: { n: 1 } },
        { aggregateId: 'mission-1', type: 'event-2', payload: { n: 2 } },
      ])

      expect(events).toHaveLength(2)
      expect(events[0].sequence).toBe(1)
      expect(events[1].sequence).toBe(2)
    })
  })

  describe('read', () => {
    beforeEach(() => {
      store.append('mission-1', 'type-a', { n: 1 })
      store.append('mission-1', 'type-b', { n: 2 })
      store.append('mission-2', 'type-a', { n: 3 })
    })

    it('reads all events', () => {
      const events = store.read()
      expect(events).toHaveLength(3)
    })

    it('filters by aggregate ID', () => {
      const events = store.read({ aggregateId: 'mission-1' })
      expect(events).toHaveLength(2)
      expect(events.every((e) => e.aggregateId === 'mission-1')).toBe(true)
    })

    it('filters by event type', () => {
      const events = store.read({ type: 'type-a' })
      expect(events).toHaveLength(2)
      expect(events.every((e) => e.type === 'type-a')).toBe(true)
    })

    it('filters by sequence number', () => {
      const events = store.read({ aggregateId: 'mission-1', afterSequence: 1 })
      expect(events).toHaveLength(1)
      expect(events[0].sequence).toBe(2)
    })

    it('limits results', () => {
      const events = store.read({ limit: 2 })
      expect(events).toHaveLength(2)
    })
  })

  describe('getAggregateEvents', () => {
    it('returns events for specific aggregate', () => {
      store.append('mission-1', 'event', {})
      store.append('mission-2', 'event', {})
      store.append('mission-1', 'event', {})

      const events = store.getAggregateEvents('mission-1')
      expect(events).toHaveLength(2)
    })

    it('returns events after sequence', () => {
      store.append('mission-1', 'event-1', {})
      store.append('mission-1', 'event-2', {})
      store.append('mission-1', 'event-3', {})

      const events = store.getAggregateEvents('mission-1', 1)
      expect(events).toHaveLength(2)
      expect(events[0].type).toBe('event-2')
    })
  })

  describe('snapshots', () => {
    it('saves and loads snapshot', () => {
      const state = { counter: 42, items: ['a', 'b'] }
      const snapshot = store.saveSnapshot('mission-1', state, 5)

      expect(snapshot.aggregateId).toBe('mission-1')
      expect(snapshot.version).toBe(5)
      expect(snapshot.state).toEqual(state)

      // Clear cache and reload
      store.clearCache()
      const loaded = store.getSnapshot('mission-1')
      expect(loaded).toEqual(snapshot)
    })

    it('returns null for non-existent snapshot', () => {
      const snapshot = store.getSnapshot('non-existent')
      expect(snapshot).toBeNull()
    })

    it('deletes snapshot', () => {
      store.saveSnapshot('mission-1', { data: 'test' })
      expect(store.getSnapshot('mission-1')).not.toBeNull()

      const deleted = store.deleteSnapshot('mission-1')
      expect(deleted).toBe(true)
      expect(store.getSnapshot('mission-1')).toBeNull()
    })

    it('uses snapshot in rebuild', () => {
      // Create events
      store.append('mission-1', 'increment', { amount: 1 })
      store.append('mission-1', 'increment', { amount: 2 })

      // Save snapshot at version 2
      store.saveSnapshot('mission-1', { total: 3 }, 2)

      // Add more events
      store.append('mission-1', 'increment', { amount: 5 })

      // Rebuild should use snapshot + events after version 2
      type State = { total: number }
      const state = store.rebuild<State>(
        'mission-1',
        (state, event) => {
          if (event.type === 'increment') {
            return { total: state.total + (event.payload as { amount: number }).amount }
          }
          return state
        },
        { total: 0 }
      )

      // Should be 3 (from snapshot) + 5 (from event after snapshot)
      expect(state.total).toBe(8)
    })
  })

  describe('rebuild', () => {
    it('rebuilds state from events', () => {
      store.append('counter', 'increment', { amount: 1 })
      store.append('counter', 'increment', { amount: 5 })
      store.append('counter', 'decrement', { amount: 2 })

      type State = { value: number }
      const state = store.rebuild<State>(
        'counter',
        (state, event) => {
          const amount = (event.payload as { amount: number }).amount
          if (event.type === 'increment') {
            return { value: state.value + amount }
          }
          if (event.type === 'decrement') {
            return { value: state.value - amount }
          }
          return state
        },
        { value: 0 }
      )

      expect(state.value).toBe(4) // 0 + 1 + 5 - 2
    })

    it('uses initial state when no events', () => {
      type State = { value: number }
      const state = store.rebuild<State>(
        'empty',
        (state) => state,
        { value: 100 }
      )

      expect(state.value).toBe(100)
    })
  })

  describe('utility methods', () => {
    it('getEventCount returns total events', () => {
      store.append('m1', 'e', {})
      store.append('m2', 'e', {})
      store.append('m1', 'e', {})

      expect(store.getEventCount()).toBe(3)
    })

    it('getEventCount filters by aggregate', () => {
      store.append('m1', 'e', {})
      store.append('m2', 'e', {})
      store.append('m1', 'e', {})

      expect(store.getEventCount('m1')).toBe(2)
    })

    it('getAggregateIds returns all unique IDs', () => {
      store.append('m1', 'e', {})
      store.append('m2', 'e', {})
      store.append('m1', 'e', {})

      const ids = store.getAggregateIds()
      expect(ids).toHaveLength(2)
      expect(ids).toContain('m1')
      expect(ids).toContain('m2')
    })

    it('getLatestSequence returns max sequence', () => {
      store.append('m1', 'e', {})
      store.append('m1', 'e', {})
      store.append('m1', 'e', {})

      expect(store.getLatestSequence('m1')).toBe(3)
    })

    it('getLatestSequence returns 0 for unknown aggregate', () => {
      expect(store.getLatestSequence('unknown')).toBe(0)
    })
  })
})

describe('singleton functions', () => {
  const testDir = join(tmpdir(), 'delta9-event-store-singleton-' + Date.now())

  beforeEach(() => {
    clearEventStore()
    mkdirSync(testDir, { recursive: true })
    mkdirSync(join(testDir, '.delta9'), { recursive: true })
  })

  afterEach(() => {
    clearEventStore()
    if (existsSync(testDir)) {
      rmSync(testDir, { recursive: true, force: true })
    }
  })

  it('getEventStore returns singleton', () => {
    const store1 = getEventStore(testDir)
    const store2 = getEventStore(testDir)
    expect(store1).toBe(store2)
  })
})

describe('history integration', () => {
  it('historyToVersionedEvent converts correctly', () => {
    const historyEvent = {
      type: 'task_completed' as const,
      timestamp: '2025-01-01T00:00:00.000Z',
      missionId: 'mission-1',
      taskId: 'task-1',
      data: { result: 'success' },
    }

    const versioned = historyToVersionedEvent(historyEvent, 5)

    expect(versioned.version).toBe(1)
    expect(versioned.type).toBe('task_completed')
    expect(versioned.timestamp).toBe('2025-01-01T00:00:00.000Z')
    expect(versioned.aggregateId).toBe('mission-1')
    expect(versioned.sequence).toBe(5)
    expect(versioned.payload).toEqual(historyEvent)
  })
})
