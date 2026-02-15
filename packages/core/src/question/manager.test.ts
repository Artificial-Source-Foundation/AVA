/**
 * Question Manager Tests
 */

import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  clearQuestionManager,
  createQuestionManager,
  getQuestionManager,
  QuestionManager,
  setQuestionManager,
} from './manager.js'
import type { QuestionEvent } from './types.js'

afterEach(() => {
  clearQuestionManager()
  vi.useRealTimers()
})

// ============================================================================
// ask / answer flow
// ============================================================================

describe('QuestionManager ask/answer', () => {
  it('resolves when answered', async () => {
    const manager = new QuestionManager()
    const promise = manager.ask({ id: 'q1', text: 'What color?' })
    manager.answer('q1', 'blue')
    const result = await promise
    expect(result.answer).toBe('blue')
    expect(result.questionId).toBe('q1')
    expect(result.answeredAt).toBeDefined()
  })

  it('supports multiple answers', async () => {
    const manager = new QuestionManager()
    const promise = manager.ask({ id: 'q1', text: 'Pick colors', multiSelect: true })
    manager.answer('q1', 'blue', ['blue', 'red'])
    const result = await promise
    expect(result.answers).toEqual(['blue', 'red'])
  })

  it('returns false when answering unknown question', () => {
    const manager = new QuestionManager()
    expect(manager.answer('nonexistent', 'answer')).toBe(false)
  })

  it('handles multiple concurrent questions', async () => {
    const manager = new QuestionManager()
    const p1 = manager.ask({ id: 'q1', text: 'Q1?' })
    const p2 = manager.ask({ id: 'q2', text: 'Q2?' })
    manager.answer('q2', 'answer2')
    manager.answer('q1', 'answer1')
    const [r1, r2] = await Promise.all([p1, p2])
    expect(r1.answer).toBe('answer1')
    expect(r2.answer).toBe('answer2')
  })
})

// ============================================================================
// cancel
// ============================================================================

describe('QuestionManager cancel', () => {
  it('rejects promise on cancel', async () => {
    const manager = new QuestionManager()
    const promise = manager.ask({ id: 'q1', text: 'Q?' })
    manager.cancel('q1')
    await expect(promise).rejects.toThrow('cancelled')
  })

  it('returns false for cancelling unknown question', () => {
    const manager = new QuestionManager()
    expect(manager.cancel('nonexistent')).toBe(false)
  })
})

// ============================================================================
// timeout
// ============================================================================

describe('QuestionManager timeout', () => {
  it('rejects after timeout', async () => {
    vi.useFakeTimers()
    const manager = new QuestionManager({ timeout: 1000 })
    const promise = manager.ask({ id: 'q1', text: 'Q?' })
    vi.advanceTimersByTime(1000)
    await expect(promise).rejects.toThrow('timed out')
  })

  it('clears timeout when answered', async () => {
    vi.useFakeTimers()
    const manager = new QuestionManager({ timeout: 1000 })
    const promise = manager.ask({ id: 'q1', text: 'Q?' })
    manager.answer('q1', 'fast')
    vi.advanceTimersByTime(2000)
    const result = await promise
    expect(result.answer).toBe('fast')
  })

  it('uses default 5 minute timeout', () => {
    vi.useFakeTimers()
    const manager = new QuestionManager()
    const promise = manager.ask({ id: 'q1', text: 'Q?' })
    // At 4 minutes, should still be pending
    vi.advanceTimersByTime(4 * 60 * 1000)
    expect(manager.isPending('q1')).toBe(true)
    // At 5 minutes, should timeout
    vi.advanceTimersByTime(1 * 60 * 1000)
    expect(manager.isPending('q1')).toBe(false)
    // Suppress unhandled rejection
    promise.catch(() => {})
  })
})

// ============================================================================
// Query methods
// ============================================================================

describe('QuestionManager queries', () => {
  it('getPending returns pending questions', async () => {
    const manager = new QuestionManager()
    const p1 = manager.ask({ id: 'q1', text: 'Q1?' })
    const p2 = manager.ask({ id: 'q2', text: 'Q2?' })
    const pending = manager.getPending()
    expect(pending).toHaveLength(2)
    expect(pending.map((q) => q.id)).toEqual(['q1', 'q2'])
    manager.clear()
    await p1.catch(() => {})
    await p2.catch(() => {})
  })

  it('isPending returns correct state', async () => {
    const manager = new QuestionManager()
    const p = manager.ask({ id: 'q1', text: 'Q?' })
    expect(manager.isPending('q1')).toBe(true)
    expect(manager.isPending('nonexistent')).toBe(false)
    manager.answer('q1', 'done')
    await p
    expect(manager.isPending('q1')).toBe(false)
  })

  it('pendingCount tracks count', async () => {
    const manager = new QuestionManager()
    expect(manager.pendingCount).toBe(0)
    const p1 = manager.ask({ id: 'q1', text: 'Q1?' })
    expect(manager.pendingCount).toBe(1)
    const p2 = manager.ask({ id: 'q2', text: 'Q2?' })
    expect(manager.pendingCount).toBe(2)
    manager.answer('q1', 'done')
    await p1
    expect(manager.pendingCount).toBe(1)
    manager.clear()
    await p2.catch(() => {})
  })
})

// ============================================================================
// clear / destroy
// ============================================================================

describe('QuestionManager lifecycle', () => {
  it('clear rejects all pending questions', async () => {
    const manager = new QuestionManager()
    const p1 = manager.ask({ id: 'q1', text: 'Q1?' })
    const p2 = manager.ask({ id: 'q2', text: 'Q2?' })
    manager.clear()
    await expect(p1).rejects.toThrow('cleared')
    await expect(p2).rejects.toThrow('cleared')
    expect(manager.pendingCount).toBe(0)
  })

  it('destroy cleans up', async () => {
    const manager = new QuestionManager()
    const p = manager.ask({ id: 'q1', text: 'Q?' })
    manager.destroy()
    await expect(p).rejects.toThrow()
    expect(manager.pendingCount).toBe(0)
  })
})

// ============================================================================
// Events
// ============================================================================

describe('QuestionManager events', () => {
  it('emits question_asked event', async () => {
    const events: QuestionEvent[] = []
    const manager = new QuestionManager({ onEvent: (e) => events.push(e) })
    const p = manager.ask({ id: 'q1', text: 'Q?' })
    expect(events).toHaveLength(1)
    expect(events[0].type).toBe('question_asked')
    manager.clear()
    await p.catch(() => {})
  })

  it('emits question_answered event', () => {
    const events: QuestionEvent[] = []
    const manager = new QuestionManager({ onEvent: (e) => events.push(e) })
    manager.ask({ id: 'q1', text: 'Q?' })
    manager.answer('q1', 'yes')
    expect(events[1].type).toBe('question_answered')
  })

  it('emits question_cancelled event', () => {
    const events: QuestionEvent[] = []
    const manager = new QuestionManager({ onEvent: (e) => events.push(e) })
    const p = manager.ask({ id: 'q1', text: 'Q?' })
    manager.cancel('q1')
    expect(events[1].type).toBe('question_cancelled')
    p.catch(() => {})
  })

  it('emits question_timeout event', () => {
    vi.useFakeTimers()
    const events: QuestionEvent[] = []
    const manager = new QuestionManager({ timeout: 100, onEvent: (e) => events.push(e) })
    const p = manager.ask({ id: 'q1', text: 'Q?' })
    vi.advanceTimersByTime(100)
    expect(events.some((e) => e.type === 'question_timeout')).toBe(true)
    p.catch(() => {})
  })
})

// ============================================================================
// Factory / Singleton
// ============================================================================

describe('factory and singleton', () => {
  it('createQuestionManager creates new instance', () => {
    const m1 = createQuestionManager()
    const m2 = createQuestionManager()
    expect(m1).not.toBe(m2)
  })

  it('getQuestionManager returns singleton', () => {
    const a = getQuestionManager()
    const b = getQuestionManager()
    expect(a).toBe(b)
  })

  it('setQuestionManager replaces singleton', () => {
    const custom = new QuestionManager()
    setQuestionManager(custom)
    expect(getQuestionManager()).toBe(custom)
  })

  it('clearQuestionManager destroys and clears', () => {
    const old = getQuestionManager()
    clearQuestionManager()
    const fresh = getQuestionManager()
    expect(fresh).not.toBe(old)
  })
})
