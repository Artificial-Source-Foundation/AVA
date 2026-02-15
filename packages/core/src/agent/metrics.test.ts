/**
 * Agent Metrics Tests
 */

import { afterEach, describe, expect, it } from 'vitest'
import {
  createMetricsCollector,
  getMetricsCollector,
  MetricsCollector,
  setMetricsCollector,
} from './metrics.js'
import type { AgentEvent } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

function makeEvent(type: AgentEvent['type'], extra?: Record<string, unknown>): AgentEvent {
  return {
    type,
    agentId: 'test-agent',
    timestamp: Date.now(),
    ...extra,
  } as AgentEvent
}

afterEach(() => {
  setMetricsCollector(null)
})

// ============================================================================
// MetricsCollector
// ============================================================================

describe('MetricsCollector', () => {
  it('creates empty metrics on first record', () => {
    const collector = new MetricsCollector()
    collector.record('s1', makeEvent('turn:start'))
    const metrics = collector.getMetrics('s1')
    expect(metrics).toBeDefined()
    expect(metrics!.sessionId).toBe('s1')
  })

  it('tracks turns', () => {
    const collector = new MetricsCollector()
    collector.record('s1', makeEvent('turn:start'))
    collector.record('s1', makeEvent('turn:start'))
    expect(collector.getMetrics('s1')!.totalTurns).toBe(2)
  })

  it('tracks tool calls from turn:finish', () => {
    const collector = new MetricsCollector()
    collector.record(
      's1',
      makeEvent('turn:finish', {
        turn: 0,
        toolCalls: [{ name: 'read_file' }, { name: 'grep' }, { name: 'read_file' }],
      })
    )
    const metrics = collector.getMetrics('s1')!
    expect(metrics.toolCalls.read_file).toBe(2)
    expect(metrics.toolCalls.grep).toBe(1)
  })

  it('tracks token usage from turn:finish', () => {
    const collector = new MetricsCollector()
    collector.record(
      's1',
      makeEvent('turn:finish', {
        turn: 0,
        toolCalls: [],
        tokensIn: 100,
        tokensOut: 50,
      })
    )
    collector.record(
      's1',
      makeEvent('turn:finish', {
        turn: 1,
        toolCalls: [],
        tokensIn: 200,
        tokensOut: 75,
      })
    )
    const metrics = collector.getMetrics('s1')!
    expect(metrics.totalTokensIn).toBe(300)
    expect(metrics.totalTokensOut).toBe(125)
  })

  it('tracks errors', () => {
    const collector = new MetricsCollector()
    collector.record('s1', makeEvent('tool:error', { toolName: 'bash', error: 'fail' }))
    collector.record('s1', makeEvent('error', { error: 'generic' }))
    expect(collector.getMetrics('s1')!.errors).toBe(2)
  })

  it('tracks recoveries', () => {
    const collector = new MetricsCollector()
    collector.record('s1', makeEvent('recovery:start', { reason: 'MAX_TURNS', turn: 5 }))
    expect(collector.getMetrics('s1')!.recoveries).toBe(1)
  })

  it('tracks duration from agent:start to agent:finish', () => {
    const collector = new MetricsCollector()
    const start = Date.now()
    collector.record('s1', makeEvent('agent:start', { timestamp: start, goal: 'test', config: {} }))
    collector.record('s1', makeEvent('agent:finish', { timestamp: start + 5000, result: {} }))
    expect(collector.getMetrics('s1')!.totalDurationMs).toBe(5000)
    expect(collector.getMetrics('s1')!.completedAt).toBe(start + 5000)
  })

  it('returns undefined for unknown session', () => {
    const collector = new MetricsCollector()
    expect(collector.getMetrics('unknown')).toBeUndefined()
  })

  it('getAllMetrics returns all sessions', () => {
    const collector = new MetricsCollector()
    collector.record('s1', makeEvent('turn:start'))
    collector.record('s2', makeEvent('turn:start'))
    expect(collector.getAllMetrics()).toHaveLength(2)
  })

  it('reset removes a session', () => {
    const collector = new MetricsCollector()
    collector.record('s1', makeEvent('turn:start'))
    collector.reset('s1')
    expect(collector.getMetrics('s1')).toBeUndefined()
  })

  it('clear removes all sessions', () => {
    const collector = new MetricsCollector()
    collector.record('s1', makeEvent('turn:start'))
    collector.record('s2', makeEvent('turn:start'))
    collector.clear()
    expect(collector.getAllMetrics()).toHaveLength(0)
  })

  it('exportMetrics returns copies', () => {
    const collector = new MetricsCollector()
    collector.record(
      's1',
      makeEvent('turn:finish', {
        turn: 0,
        toolCalls: [{ name: 'read_file' }],
      })
    )
    const exported = collector.exportMetrics()
    exported[0].toolCalls.read_file = 999
    expect(collector.getMetrics('s1')!.toolCalls.read_file).toBe(1)
  })

  it('sessionCount returns tracked sessions', () => {
    const collector = new MetricsCollector()
    expect(collector.sessionCount).toBe(0)
    collector.record('s1', makeEvent('turn:start'))
    expect(collector.sessionCount).toBe(1)
  })
})

// ============================================================================
// Singleton
// ============================================================================

describe('singleton', () => {
  it('getMetricsCollector returns same instance', () => {
    expect(getMetricsCollector()).toBe(getMetricsCollector())
  })

  it('createMetricsCollector creates new instance', () => {
    expect(createMetricsCollector()).not.toBe(createMetricsCollector())
  })
})
