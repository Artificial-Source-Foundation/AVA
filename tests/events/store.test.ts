/**
 * Event Store Tests
 */

import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { EventStore, resetEventStore } from '../../src/events/store.js'
import { existsSync, rmSync, mkdirSync } from 'node:fs'
import { join } from 'node:path'
import { tmpdir } from 'node:os'

describe('EventStore', () => {
  let store: EventStore
  let testDir: string

  beforeEach(() => {
    testDir = join(tmpdir(), `delta9-test-${Date.now()}`)
    mkdirSync(testDir, { recursive: true })
    store = new EventStore({ baseDir: testDir })
  })

  afterEach(() => {
    resetEventStore()
    if (existsSync(testDir)) {
      rmSync(testDir, { recursive: true, force: true })
    }
  })

  describe('append', () => {
    it('should append events with generated ID and timestamp', () => {
      const event = store.append('mission.created', {
        name: 'Test Mission',
        objectives: ['obj1', 'obj2'],
      })

      expect(event.id).toBeDefined()
      expect(event.type).toBe('mission.created')
      expect(event.timestamp).toBeDefined()
      expect(event.data.name).toBe('Test Mission')
    })

    it('should include optional session and mission IDs', () => {
      const event = store.append(
        'task.created',
        { taskId: 't1', title: 'Task 1' },
        { sessionId: 'sess1', missionId: 'miss1' }
      )

      expect(event.sessionId).toBe('sess1')
      expect(event.missionId).toBe('miss1')
    })

    it('should persist events to disk', () => {
      store.append('mission.created', {
        name: 'Persisted Mission',
        objectives: [],
      })

      const eventsFile = join(testDir, '.delta9', 'events.jsonl')
      expect(existsSync(eventsFile)).toBe(true)
    })
  })

  describe('query', () => {
    beforeEach(() => {
      store.append('mission.created', { name: 'M1', objectives: [] }, { missionId: 'm1' })
      store.append('task.created', { taskId: 't1', title: 'T1' }, { missionId: 'm1' })
      store.append('task.created', { taskId: 't2', title: 'T2' }, { missionId: 'm1' })
      store.append('task.completed', { taskId: 't1', success: true, duration: 1000 })
      store.append('mission.created', { name: 'M2', objectives: [] }, { missionId: 'm2' })
    })

    it('should return all events without filters', () => {
      const events = store.query()
      expect(events.length).toBe(5)
    })

    it('should filter by event types', () => {
      const events = store.query({ types: ['mission.created'] })
      expect(events.length).toBe(2)
      expect(events.every((e) => e.type === 'mission.created')).toBe(true)
    })

    it('should filter by category', () => {
      const events = store.query({ category: 'task' })
      expect(events.length).toBe(3)
    })

    it('should filter by mission ID', () => {
      const events = store.query({ missionId: 'm1' })
      expect(events.length).toBe(3)
    })

    it('should support limit and offset', () => {
      const events = store.query({ limit: 2, offset: 1 })
      expect(events.length).toBe(2)
      expect(events[0].type).toBe('task.created')
    })

    it('should support descending order', () => {
      const events = store.query({ order: 'desc', limit: 2 })
      expect(events[0].type).toBe('mission.created')
      expect((events[0].data as { name: string }).name).toBe('M2')
    })
  })

  describe('getLastOfType', () => {
    it('should return the last event of a specific type', () => {
      store.append('task.created', { taskId: 't1', title: 'First' })
      store.append('task.created', { taskId: 't2', title: 'Second' })
      store.append('mission.created', { name: 'M1', objectives: [] })

      const last = store.getLastOfType('task.created')
      expect(last).toBeDefined()
      expect(last?.data.title).toBe('Second')
    })

    it('should return null if no events of type exist', () => {
      store.append('mission.created', { name: 'M1', objectives: [] })

      const last = store.getLastOfType('task.created')
      expect(last).toBeNull()
    })
  })

  describe('getTaskEvents', () => {
    it('should return all events for a specific task', () => {
      store.append('task.created', { taskId: 't1', title: 'Task 1' })
      store.append('task.started', { taskId: 't1', agent: 'operator' })
      store.append('task.created', { taskId: 't2', title: 'Task 2' })
      store.append('task.completed', { taskId: 't1', success: true, duration: 1000 })

      const events = store.getTaskEvents('t1')
      expect(events.length).toBe(3)
    })
  })

  describe('session management', () => {
    it('should start a session and track session ID', () => {
      const sessionId = store.startSession()

      expect(sessionId).toBeDefined()
      expect(store.getCurrentSessionId()).toBe(sessionId)

      const events = store.query({ types: ['system.session_started'] })
      expect(events.length).toBe(1)
    })

    it('should end a session', () => {
      store.startSession()
      store.endSession('completed')

      expect(store.getCurrentSessionId()).toBeNull()

      const events = store.query({ types: ['system.session_ended'] })
      expect(events.length).toBe(1)
    })

    it('should use custom session ID', () => {
      const sessionId = store.startSession('custom-session')
      expect(sessionId).toBe('custom-session')
    })
  })

  describe('replay', () => {
    it('should replay events through a reducer', () => {
      store.append('task.completed', { taskId: 't1', success: true, duration: 100 })
      store.append('task.completed', { taskId: 't2', success: true, duration: 200 })
      store.append('task.completed', { taskId: 't3', success: false, duration: 50 })

      interface State {
        total: number
        successful: number
        totalDuration: number
      }

      const state = store.replay<State>(
        (s, event) => {
          if (event.type === 'task.completed') {
            return {
              total: s.total + 1,
              successful: event.data.success ? s.successful + 1 : s.successful,
              totalDuration: s.totalDuration + event.data.duration,
            }
          }
          return s
        },
        { total: 0, successful: 0, totalDuration: 0 }
      )

      expect(state.total).toBe(3)
      expect(state.successful).toBe(2)
      expect(state.totalDuration).toBe(350)
    })
  })

  describe('getStats', () => {
    it('should return event statistics', () => {
      store.startSession()
      store.append('mission.created', { name: 'M1', objectives: [] }, { missionId: 'm1' })
      store.append('task.created', { taskId: 't1', title: 'T1' })
      store.append('task.created', { taskId: 't2', title: 'T2' })

      const stats = store.getStats()

      expect(stats.totalEvents).toBe(4) // session_started + 3 others
      expect(stats.eventsByType['mission.created']).toBe(1)
      expect(stats.eventsByType['task.created']).toBe(2)
      expect(stats.eventsByCategory['mission']).toBe(1)
      expect(stats.eventsByCategory['task']).toBe(2)
      expect(stats.sessions).toBe(1)
      expect(stats.missions).toBe(1)
    })
  })

  describe('persistence', () => {
    it('should load events from disk on initialization', () => {
      // Create events
      store.append('mission.created', { name: 'Persistent', objectives: [] })
      store.append('task.created', { taskId: 't1', title: 'Task' })

      // Create new store instance (should load from disk)
      const newStore = new EventStore({ baseDir: testDir })

      const events = newStore.query()
      expect(events.length).toBe(2)
      expect((events[0].data as { name: string }).name).toBe('Persistent')
    })

    it('should compact old events', () => {
      // Create events
      store.append('mission.created', { name: 'M1', objectives: [] })
      store.append('task.created', { taskId: 't1', title: 'T1' })

      // Compact with 30 days should keep recent events
      const removed = store.compact(30)
      expect(removed).toBe(0) // Events from now are not older than 30 days

      const events = store.query()
      expect(events.length).toBe(2)
    })
  })

  describe('export', () => {
    it('should export events as JSON', () => {
      store.append('mission.created', { name: 'Export Test', objectives: [] })

      const exported = store.export()
      const parsed = JSON.parse(exported)

      expect(Array.isArray(parsed)).toBe(true)
      expect(parsed.length).toBe(1)
      expect(parsed[0].data.name).toBe('Export Test')
    })
  })
})
