/**
 * Tests for Agent Events Module
 */

import { describe, expect, it, vi } from 'vitest'
import {
  AgentEventEmitter,
  createBufferedCallback,
  createEventBuffer,
  createEventEmitter,
  EventBuffer,
  filterByType,
  getErrorEvents,
  getEventDuration,
  getEventStats,
  getThoughts,
  getToolEvents,
  getTotalDuration,
  getTurnDurations,
} from './events.js'
import type { AgentEvent, AgentEventType, AgentResult, AgentTerminateMode } from './types.js'

// ============================================================================
// Test Helpers
// ============================================================================

function makeEvent(type: AgentEventType, extra: Record<string, unknown> = {}): AgentEvent {
  return { type, agentId: 'test', timestamp: Date.now(), ...extra } as AgentEvent
}

// ============================================================================
// AgentEventEmitter Tests
// ============================================================================

describe('AgentEventEmitter', () => {
  it('on adds listener and returns unsubscribe function', () => {
    const emitter = new AgentEventEmitter()
    const callback = vi.fn()

    const unsubscribe = emitter.on(callback)

    expect(typeof unsubscribe).toBe('function')

    const event = makeEvent('thought', { text: 'test' })
    emitter.emit(event)

    expect(callback).toHaveBeenCalledWith(event)
    expect(callback).toHaveBeenCalledTimes(1)
  })

  it('off removes a listener', () => {
    const emitter = new AgentEventEmitter()
    const callback = vi.fn()

    emitter.on(callback)
    emitter.emit(makeEvent('thought'))
    expect(callback).toHaveBeenCalledTimes(1)

    emitter.off(callback)
    emitter.emit(makeEvent('thought'))
    expect(callback).toHaveBeenCalledTimes(1) // Not called again
  })

  it('emit calls all general listeners', () => {
    const emitter = new AgentEventEmitter()
    const callback1 = vi.fn()
    const callback2 = vi.fn()

    emitter.on(callback1)
    emitter.on(callback2)

    const event = makeEvent('thought')
    emitter.emit(event)

    expect(callback1).toHaveBeenCalledWith(event)
    expect(callback2).toHaveBeenCalledWith(event)
  })

  it('onType adds type-specific listener', () => {
    const emitter = new AgentEventEmitter()
    const callback = vi.fn()

    emitter.onType('thought', callback)

    emitter.emit(makeEvent('thought'))
    expect(callback).toHaveBeenCalledTimes(1)

    emitter.emit(makeEvent('tool:start'))
    expect(callback).toHaveBeenCalledTimes(1) // Not called for different type
  })

  it('offType removes type-specific listener', () => {
    const emitter = new AgentEventEmitter()
    const callback = vi.fn()

    emitter.onType('thought', callback)
    emitter.emit(makeEvent('thought'))
    expect(callback).toHaveBeenCalledTimes(1)

    emitter.offType('thought', callback)
    emitter.emit(makeEvent('thought'))
    expect(callback).toHaveBeenCalledTimes(1) // Not called again
  })

  it('emit calls both general and type-specific listeners', () => {
    const emitter = new AgentEventEmitter()
    const generalCallback = vi.fn()
    const typeCallback = vi.fn()

    emitter.on(generalCallback)
    emitter.onType('thought', typeCallback)

    const event = makeEvent('thought')
    emitter.emit(event)

    expect(generalCallback).toHaveBeenCalledWith(event)
    expect(typeCallback).toHaveBeenCalledWith(event)
  })

  it('listener errors are silently caught', () => {
    const emitter = new AgentEventEmitter()
    const errorCallback = vi.fn(() => {
      throw new Error('Listener error')
    })
    const successCallback = vi.fn()

    emitter.on(errorCallback)
    emitter.on(successCallback)

    const event = makeEvent('thought')
    expect(() => emitter.emit(event)).not.toThrow()

    expect(errorCallback).toHaveBeenCalledWith(event)
    expect(successCallback).toHaveBeenCalledWith(event) // Still called
  })

  it('clear removes all listeners', () => {
    const emitter = new AgentEventEmitter()
    const generalCallback = vi.fn()
    const typeCallback = vi.fn()

    emitter.on(generalCallback)
    emitter.onType('thought', typeCallback)

    emitter.clear()

    emitter.emit(makeEvent('thought'))
    expect(generalCallback).not.toHaveBeenCalled()
    expect(typeCallback).not.toHaveBeenCalled()
  })

  it('unsubscribe function from on works correctly', () => {
    const emitter = new AgentEventEmitter()
    const callback = vi.fn()

    const unsubscribe = emitter.on(callback)
    emitter.emit(makeEvent('thought'))
    expect(callback).toHaveBeenCalledTimes(1)

    unsubscribe()
    emitter.emit(makeEvent('thought'))
    expect(callback).toHaveBeenCalledTimes(1)
  })

  it('unsubscribe function from onType works correctly', () => {
    const emitter = new AgentEventEmitter()
    const callback = vi.fn()

    const unsubscribe = emitter.onType('thought', callback)
    emitter.emit(makeEvent('thought'))
    expect(callback).toHaveBeenCalledTimes(1)

    unsubscribe()
    emitter.emit(makeEvent('thought'))
    expect(callback).toHaveBeenCalledTimes(1)
  })

  it('removing non-existent listener is a no-op', () => {
    const emitter = new AgentEventEmitter()
    const callback = vi.fn()

    expect(() => emitter.off(callback)).not.toThrow()
    expect(() => emitter.offType('thought', callback)).not.toThrow()
  })

  it('multiple listeners called in order', () => {
    const emitter = new AgentEventEmitter()
    const order: number[] = []

    emitter.on(() => order.push(1))
    emitter.on(() => order.push(2))
    emitter.on(() => order.push(3))

    emitter.emit(makeEvent('thought'))

    expect(order).toEqual([1, 2, 3])
  })

  it('type-specific listener errors are silently caught', () => {
    const emitter = new AgentEventEmitter()
    const errorCallback = vi.fn(() => {
      throw new Error('Type listener error')
    })
    const successCallback = vi.fn()

    emitter.onType('thought', errorCallback)
    emitter.onType('thought', successCallback)

    const event = makeEvent('thought')
    expect(() => emitter.emit(event)).not.toThrow()

    expect(errorCallback).toHaveBeenCalledWith(event)
    expect(successCallback).toHaveBeenCalledWith(event)
  })

  it('multiple type-specific listeners for same type are all called', () => {
    const emitter = new AgentEventEmitter()
    const callback1 = vi.fn()
    const callback2 = vi.fn()

    emitter.onType('thought', callback1)
    emitter.onType('thought', callback2)

    const event = makeEvent('thought')
    emitter.emit(event)

    expect(callback1).toHaveBeenCalledWith(event)
    expect(callback2).toHaveBeenCalledWith(event)
  })
})

// ============================================================================
// EventBuffer Tests
// ============================================================================

describe('EventBuffer', () => {
  it('push adds events', () => {
    const buffer = new EventBuffer()
    const event = makeEvent('thought')

    buffer.push(event)

    expect(buffer.size).toBe(1)
    expect(buffer.getAll()).toContain(event)
  })

  it('getAll returns all events', () => {
    const buffer = new EventBuffer()
    const event1 = makeEvent('thought')
    const event2 = makeEvent('tool:start')

    buffer.push(event1)
    buffer.push(event2)

    const all = buffer.getAll()
    expect(all).toHaveLength(2)
    expect(all).toEqual([event1, event2])
  })

  it('getSince filters by timestamp', () => {
    const buffer = new EventBuffer()
    const now = Date.now()

    const event1 = makeEvent('thought', { timestamp: now - 1000 })
    const event2 = makeEvent('thought', { timestamp: now })
    const event3 = makeEvent('thought', { timestamp: now + 1000 })

    buffer.push(event1)
    buffer.push(event2)
    buffer.push(event3)

    const recent = buffer.getSince(now)
    expect(recent).toHaveLength(2)
    expect(recent).toContain(event2)
    expect(recent).toContain(event3)
  })

  it('getLast returns last N events', () => {
    const buffer = new EventBuffer()
    const event1 = makeEvent('thought', { text: '1' })
    const event2 = makeEvent('thought', { text: '2' })
    const event3 = makeEvent('thought', { text: '3' })

    buffer.push(event1)
    buffer.push(event2)
    buffer.push(event3)

    const last2 = buffer.getLast(2)
    expect(last2).toHaveLength(2)
    expect(last2).toEqual([event2, event3])
  })

  it('clear removes all events', () => {
    const buffer = new EventBuffer()
    buffer.push(makeEvent('thought'))
    buffer.push(makeEvent('thought'))

    expect(buffer.size).toBe(2)

    buffer.clear()

    expect(buffer.size).toBe(0)
    expect(buffer.getAll()).toEqual([])
  })

  it('size getter is correct', () => {
    const buffer = new EventBuffer()

    expect(buffer.size).toBe(0)

    buffer.push(makeEvent('thought'))
    expect(buffer.size).toBe(1)

    buffer.push(makeEvent('thought'))
    expect(buffer.size).toBe(2)

    buffer.clear()
    expect(buffer.size).toBe(0)
  })

  it('maxSize trims oldest events on overflow', () => {
    const buffer = new EventBuffer(3)
    const event1 = makeEvent('thought', { text: '1' })
    const event2 = makeEvent('thought', { text: '2' })
    const event3 = makeEvent('thought', { text: '3' })
    const event4 = makeEvent('thought', { text: '4' })

    buffer.push(event1)
    buffer.push(event2)
    buffer.push(event3)
    buffer.push(event4)

    expect(buffer.size).toBe(3)
    const all = buffer.getAll()
    expect(all).not.toContain(event1) // Oldest removed
    expect(all).toContain(event2)
    expect(all).toContain(event3)
    expect(all).toContain(event4)
  })

  it('default maxSize is 1000', () => {
    const buffer = new EventBuffer()

    // Push 1001 events
    for (let i = 0; i < 1001; i++) {
      buffer.push(makeEvent('thought', { text: String(i) }))
    }

    expect(buffer.size).toBe(1000)
  })

  it('custom maxSize works', () => {
    const buffer = new EventBuffer(5)

    for (let i = 0; i < 10; i++) {
      buffer.push(makeEvent('thought', { text: String(i) }))
    }

    expect(buffer.size).toBe(5)
  })

  it('empty buffer: getAll returns [], getLast returns [], size is 0', () => {
    const buffer = new EventBuffer()

    expect(buffer.getAll()).toEqual([])
    expect(buffer.getLast(5)).toEqual([])
    expect(buffer.size).toBe(0)
  })

  it('getSince with future timestamp returns empty array', () => {
    const buffer = new EventBuffer()
    buffer.push(makeEvent('thought', { timestamp: Date.now() }))

    const future = Date.now() + 10000
    expect(buffer.getSince(future)).toEqual([])
  })

  it('getLast with N greater than size returns all events', () => {
    const buffer = new EventBuffer()
    const event1 = makeEvent('thought')
    const event2 = makeEvent('thought')

    buffer.push(event1)
    buffer.push(event2)

    const last10 = buffer.getLast(10)
    expect(last10).toHaveLength(2)
    expect(last10).toEqual([event1, event2])
  })
})

// ============================================================================
// filterByType Tests
// ============================================================================

describe('filterByType', () => {
  it('filters events by type', () => {
    const events = [
      makeEvent('thought', { text: 'thinking' }),
      makeEvent('tool:start', { toolName: 'read_file' }),
      makeEvent('thought', { text: 'more thinking' }),
      makeEvent('tool:finish', { toolName: 'read_file' }),
    ]

    const thoughts = filterByType(events, 'thought')
    expect(thoughts).toHaveLength(2)
    expect(thoughts.every((e) => e.type === 'thought')).toBe(true)
  })

  it('returns empty array when no match', () => {
    const events = [makeEvent('thought'), makeEvent('thought')]

    const tools = filterByType(events, 'tool:start')
    expect(tools).toEqual([])
  })

  it('returns typed array', () => {
    const events = [makeEvent('thought', { text: 'test' })]

    const thoughts = filterByType(events, 'thought')
    expect(thoughts[0]?.type).toBe('thought')
  })
})

// ============================================================================
// getToolEvents Tests
// ============================================================================

describe('getToolEvents', () => {
  it('returns tool:start, tool:finish, tool:error events', () => {
    const events = [
      makeEvent('thought'),
      makeEvent('tool:start', { toolName: 'read' }),
      makeEvent('tool:finish', { toolName: 'read' }),
      makeEvent('tool:error', { toolName: 'write' }),
      makeEvent('turn:start'),
    ]

    const toolEvents = getToolEvents(events)
    expect(toolEvents).toHaveLength(3)
    expect(toolEvents.every((e) => e.type.startsWith('tool:'))).toBe(true)
  })

  it('excludes non-tool events', () => {
    const events = [makeEvent('thought'), makeEvent('turn:start'), makeEvent('agent:start')]

    const toolEvents = getToolEvents(events)
    expect(toolEvents).toEqual([])
  })

  it('empty input returns empty', () => {
    const toolEvents = getToolEvents([])
    expect(toolEvents).toEqual([])
  })
})

// ============================================================================
// getErrorEvents Tests
// ============================================================================

describe('getErrorEvents', () => {
  it('returns tool:error and error events', () => {
    const events = [
      makeEvent('thought'),
      makeEvent('tool:error', { toolName: 'read', error: 'failed' }),
      makeEvent('error', { error: 'general error' }),
      makeEvent('tool:start'),
    ]

    const errorEvents = getErrorEvents(events)
    expect(errorEvents).toHaveLength(2)
    expect(errorEvents.every((e) => e.type === 'tool:error' || e.type === 'error')).toBe(true)
  })

  it('excludes non-error events', () => {
    const events = [makeEvent('thought'), makeEvent('tool:start'), makeEvent('turn:finish')]

    const errorEvents = getErrorEvents(events)
    expect(errorEvents).toEqual([])
  })

  it('empty input returns empty', () => {
    const errorEvents = getErrorEvents([])
    expect(errorEvents).toEqual([])
  })
})

// ============================================================================
// getThoughts Tests
// ============================================================================

describe('getThoughts', () => {
  it('concatenates thought event text values', () => {
    const events = [
      makeEvent('thought', { text: 'First ' }),
      makeEvent('tool:start'),
      makeEvent('thought', { text: 'Second ' }),
      makeEvent('thought', { text: 'Third' }),
    ]

    const thoughts = getThoughts(events)
    expect(thoughts).toBe('First Second Third')
  })

  it('empty for no thoughts', () => {
    const events = [makeEvent('tool:start'), makeEvent('turn:start')]

    const thoughts = getThoughts(events)
    expect(thoughts).toBe('')
  })

  it('joins multiple thoughts without separator', () => {
    const events = [makeEvent('thought', { text: 'A' }), makeEvent('thought', { text: 'B' })]

    const thoughts = getThoughts(events)
    expect(thoughts).toBe('AB')
  })
})

// ============================================================================
// getEventDuration Tests
// ============================================================================

describe('getEventDuration', () => {
  it('returns end.timestamp - start.timestamp', () => {
    const start = makeEvent('agent:start', { timestamp: 1000 })
    const end = makeEvent('agent:finish', { timestamp: 5000 })

    const duration = getEventDuration(start, end)
    expect(duration).toBe(4000)
  })

  it('works with any two events', () => {
    const event1 = makeEvent('thought', { timestamp: 100 })
    const event2 = makeEvent('tool:start', { timestamp: 250 })

    const duration = getEventDuration(event1, event2)
    expect(duration).toBe(150)
  })
})

// ============================================================================
// getTotalDuration Tests
// ============================================================================

describe('getTotalDuration', () => {
  it('returns duration between agent:start and agent:finish', () => {
    const mockResult: AgentResult = {
      success: true,
      terminateMode: 'GOAL' as AgentTerminateMode,
      output: 'done',
      steps: [],
      tokensUsed: 0,
      durationMs: 0,
      turns: 0,
    }

    const events = [
      makeEvent('agent:start', { timestamp: 1000 }),
      makeEvent('thought', { timestamp: 2000 }),
      makeEvent('agent:finish', { timestamp: 5000, result: mockResult }),
    ]

    const duration = getTotalDuration(events)
    expect(duration).toBe(4000)
  })

  it('returns null if no start event', () => {
    const mockResult: AgentResult = {
      success: true,
      terminateMode: 'GOAL' as AgentTerminateMode,
      output: 'done',
      steps: [],
      tokensUsed: 0,
      durationMs: 0,
      turns: 0,
    }

    const events = [makeEvent('agent:finish', { result: mockResult })]

    const duration = getTotalDuration(events)
    expect(duration).toBeNull()
  })

  it('returns null if no finish event', () => {
    const events = [makeEvent('agent:start')]

    const duration = getTotalDuration(events)
    expect(duration).toBeNull()
  })
})

// ============================================================================
// getTurnDurations Tests
// ============================================================================

describe('getTurnDurations', () => {
  it('maps turn number to duration', () => {
    const events = [
      makeEvent('turn:start', { turn: 0, timestamp: 1000 }),
      makeEvent('turn:finish', { turn: 0, timestamp: 2000, toolCalls: [] }),
      makeEvent('turn:start', { turn: 1, timestamp: 3000 }),
      makeEvent('turn:finish', { turn: 1, timestamp: 4500, toolCalls: [] }),
    ]

    const durations = getTurnDurations(events)
    expect(durations.get(0)).toBe(1000)
    expect(durations.get(1)).toBe(1500)
  })

  it('handles multiple turns', () => {
    const events = [
      makeEvent('turn:start', { turn: 0, timestamp: 1000 }),
      makeEvent('turn:finish', { turn: 0, timestamp: 2000, toolCalls: [] }),
      makeEvent('turn:start', { turn: 1, timestamp: 2100 }),
      makeEvent('turn:finish', { turn: 1, timestamp: 3000, toolCalls: [] }),
      makeEvent('turn:start', { turn: 2, timestamp: 3100 }),
      makeEvent('turn:finish', { turn: 2, timestamp: 5000, toolCalls: [] }),
    ]

    const durations = getTurnDurations(events)
    expect(durations.size).toBe(3)
    expect(durations.get(0)).toBe(1000)
    expect(durations.get(1)).toBe(900)
    expect(durations.get(2)).toBe(1900)
  })

  it('skips turns without matching finish', () => {
    const events = [
      makeEvent('turn:start', { turn: 0, timestamp: 1000 }),
      makeEvent('turn:finish', { turn: 0, timestamp: 2000, toolCalls: [] }),
      makeEvent('turn:start', { turn: 1, timestamp: 3000 }),
      // No finish for turn 1
    ]

    const durations = getTurnDurations(events)
    expect(durations.size).toBe(1)
    expect(durations.get(0)).toBe(1000)
    expect(durations.has(1)).toBe(false)
  })
})

// ============================================================================
// getEventStats Tests
// ============================================================================

describe('getEventStats', () => {
  it('counts all events', () => {
    const events = [
      makeEvent('thought'),
      makeEvent('tool:start'),
      makeEvent('tool:finish'),
      makeEvent('turn:start', { turn: 0 }),
    ]

    const stats = getEventStats(events)
    expect(stats.totalEvents).toBe(4)
  })

  it('counts by type', () => {
    const events = [
      makeEvent('thought'),
      makeEvent('thought'),
      makeEvent('tool:start'),
      makeEvent('turn:start', { turn: 0 }),
    ]

    const stats = getEventStats(events)
    expect(stats.eventCounts.get('thought')).toBe(2)
    expect(stats.eventCounts.get('tool:start')).toBe(1)
    expect(stats.eventCounts.get('turn:start')).toBe(1)
  })

  it('counts errors (tool:error + error)', () => {
    const events = [
      makeEvent('tool:error', { toolName: 'read', error: 'failed' }),
      makeEvent('error', { error: 'general' }),
      makeEvent('tool:error', { toolName: 'write', error: 'failed' }),
      makeEvent('thought'),
    ]

    const stats = getEventStats(events)
    expect(stats.errorCount).toBe(3)
  })

  it('counts turns and tool calls', () => {
    const events = [
      makeEvent('turn:start', { turn: 0 }),
      makeEvent('tool:start', { toolName: 'read' }),
      makeEvent('tool:finish', { toolName: 'read' }),
      makeEvent('turn:finish', { turn: 0, toolCalls: [] }),
      makeEvent('turn:start', { turn: 1 }),
      makeEvent('tool:start', { toolName: 'write' }),
      makeEvent('tool:start', { toolName: 'bash' }),
    ]

    const stats = getEventStats(events)
    expect(stats.turnCount).toBe(2)
    expect(stats.toolCallCount).toBe(3)
  })
})

// ============================================================================
// Factory Functions Tests
// ============================================================================

describe('createEventEmitter', () => {
  it('returns new emitter', () => {
    const emitter = createEventEmitter()
    expect(emitter).toBeInstanceOf(AgentEventEmitter)

    const callback = vi.fn()
    emitter.on(callback)
    emitter.emit(makeEvent('thought'))

    expect(callback).toHaveBeenCalled()
  })
})

describe('createEventBuffer', () => {
  it('returns new buffer with optional maxSize', () => {
    const buffer1 = createEventBuffer()
    expect(buffer1).toBeInstanceOf(EventBuffer)

    const buffer2 = createEventBuffer(500)
    expect(buffer2).toBeInstanceOf(EventBuffer)

    // Verify maxSize works
    for (let i = 0; i < 600; i++) {
      buffer2.push(makeEvent('thought'))
    }
    expect(buffer2.size).toBe(500)
  })
})

describe('createBufferedCallback', () => {
  it('creates a callback that pushes to buffer', () => {
    const buffer = createEventBuffer()
    const callback = createBufferedCallback(buffer)

    const event1 = makeEvent('thought')
    const event2 = makeEvent('tool:start')

    callback(event1)
    callback(event2)

    expect(buffer.size).toBe(2)
    expect(buffer.getAll()).toContain(event1)
    expect(buffer.getAll()).toContain(event2)
  })
})
