/**
 * Activity Multiplexer Tests
 */

import { describe, expect, it } from 'vitest'
import type { WorkerActivityEvent } from '../types.js'
import {
  ActivityMultiplexer,
  createAggregator,
  createFilteredCallback,
  createTaggedCallback,
} from './activity.js'

function makeEvent(type: string, data?: Record<string, unknown>): WorkerActivityEvent {
  return {
    type: type as WorkerActivityEvent['type'],
    timestamp: Date.now(),
    data: data ?? {},
  }
}

// ============================================================================
// ActivityMultiplexer
// ============================================================================

describe('ActivityMultiplexer', () => {
  it('creates worker callbacks that tag events', () => {
    const mux = new ActivityMultiplexer()
    const events: unknown[] = []
    mux.subscribe((e) => events.push(e))

    const cb = mux.createWorkerCallback('worker-1')
    cb(makeEvent('tool_start'))

    expect(events).toHaveLength(1)
    expect((events[0] as { taskId: string }).taskId).toBe('worker-1')
  })

  it('assigns unique execution indices', () => {
    const mux = new ActivityMultiplexer()
    const events: unknown[] = []
    mux.subscribe((e) => events.push(e))

    const cb1 = mux.createWorkerCallback('w1')
    const cb2 = mux.createWorkerCallback('w2')
    cb1(makeEvent('tool_start'))
    cb2(makeEvent('tool_start'))

    expect((events[0] as { executionIndex: number }).executionIndex).toBe(0)
    expect((events[1] as { executionIndex: number }).executionIndex).toBe(1)
  })

  it('supports multiple subscribers', () => {
    const mux = new ActivityMultiplexer()
    const events1: unknown[] = []
    const events2: unknown[] = []
    mux.subscribe((e) => events1.push(e))
    mux.subscribe((e) => events2.push(e))

    const cb = mux.createWorkerCallback('w1')
    cb(makeEvent('tool_start'))

    expect(events1).toHaveLength(1)
    expect(events2).toHaveLength(1)
  })

  it('unsubscribe stops receiving events', () => {
    const mux = new ActivityMultiplexer()
    const events: unknown[] = []
    const unsub = mux.subscribe((e) => events.push(e))
    unsub()

    const cb = mux.createWorkerCallback('w1')
    cb(makeEvent('tool_start'))

    expect(events).toHaveLength(0)
  })

  it('buffers events when buffering is enabled', () => {
    const mux = new ActivityMultiplexer()
    const events: unknown[] = []
    mux.subscribe((e) => events.push(e))

    mux.startBuffering()
    const cb = mux.createWorkerCallback('w1')
    cb(makeEvent('tool_start'))

    expect(events).toHaveLength(0)
    expect(mux.getBufferSize()).toBe(1)
  })

  it('flush emits buffered events in order', () => {
    const mux = new ActivityMultiplexer()
    const events: unknown[] = []
    mux.subscribe((e) => events.push(e))

    mux.startBuffering()
    const cb = mux.createWorkerCallback('w1')
    cb(makeEvent('tool_start'))
    cb(makeEvent('tool_end'))

    mux.flush()
    expect(events).toHaveLength(2)
    expect(mux.getBufferSize()).toBe(0)
  })

  it('startGroup creates and sets group ID', () => {
    const mux = new ActivityMultiplexer()
    const events: unknown[] = []
    mux.subscribe((e) => events.push(e))

    const groupId = mux.startGroup()
    expect(groupId).toBeTruthy()
    expect(groupId).toContain('group-')

    const cb = mux.createWorkerCallback('w1')
    cb(makeEvent('tool_start'))

    expect((events[0] as { parallelGroup: string }).parallelGroup).toBe(groupId)
  })

  it('endGroup clears group ID', () => {
    const mux = new ActivityMultiplexer()
    const events: unknown[] = []
    mux.subscribe((e) => events.push(e))

    mux.startGroup()
    mux.endGroup()

    const cb = mux.createWorkerCallback('w1')
    cb(makeEvent('tool_start'))

    expect((events[0] as { parallelGroup: string | undefined }).parallelGroup).toBeUndefined()
  })

  it('getListenerCount returns correct count', () => {
    const mux = new ActivityMultiplexer()
    expect(mux.getListenerCount()).toBe(0)
    const unsub = mux.subscribe(() => {})
    expect(mux.getListenerCount()).toBe(1)
    unsub()
    expect(mux.getListenerCount()).toBe(0)
  })

  it('clear resets all state', () => {
    const mux = new ActivityMultiplexer()
    mux.subscribe(() => {})
    mux.startBuffering()
    mux.startGroup()

    mux.clear()
    expect(mux.getListenerCount()).toBe(0)
    expect(mux.getBufferSize()).toBe(0)
  })

  it('ignores listener errors', () => {
    const mux = new ActivityMultiplexer()
    const events: unknown[] = []
    mux.subscribe(() => {
      throw new Error('listener error')
    })
    mux.subscribe((e) => events.push(e))

    const cb = mux.createWorkerCallback('w1')
    cb(makeEvent('tool_start'))

    expect(events).toHaveLength(1)
  })
})

// ============================================================================
// Convenience Functions
// ============================================================================

describe('createTaggedCallback', () => {
  it('tags events with task ID', () => {
    const received: WorkerActivityEvent[] = []
    const downstream = (e: WorkerActivityEvent) => received.push(e)
    const tagged = createTaggedCallback('task-42', downstream)

    tagged(makeEvent('tool_start'))
    expect(received).toHaveLength(1)
    expect(received[0].data.taskId).toBe('task-42')
  })
})

describe('createFilteredCallback', () => {
  it('passes matching events', () => {
    const received: WorkerActivityEvent[] = []
    const downstream = (e: WorkerActivityEvent) => received.push(e)
    const filtered = createFilteredCallback(['tool_start'], downstream)

    filtered(makeEvent('tool_start'))
    filtered(makeEvent('tool_end'))

    expect(received).toHaveLength(1)
    expect(received[0].type).toBe('tool_start')
  })

  it('blocks non-matching events', () => {
    const received: WorkerActivityEvent[] = []
    const downstream = (e: WorkerActivityEvent) => received.push(e)
    const filtered = createFilteredCallback(['tool_end'], downstream)

    filtered(makeEvent('tool_start'))
    expect(received).toHaveLength(0)
  })
})

describe('createAggregator', () => {
  it('collects events', () => {
    const agg = createAggregator()
    agg.callback(makeEvent('tool_start'))
    agg.callback(makeEvent('tool_end'))

    expect(agg.getEvents()).toHaveLength(2)
  })

  it('clear empties events', () => {
    const agg = createAggregator()
    agg.callback(makeEvent('tool_start'))
    agg.clear()
    expect(agg.getEvents()).toHaveLength(0)
  })

  it('getEvents returns copy', () => {
    const agg = createAggregator()
    agg.callback(makeEvent('tool_start'))
    const events = agg.getEvents()
    events.push(makeEvent('tool_end'))
    expect(agg.getEvents()).toHaveLength(1)
  })
})
