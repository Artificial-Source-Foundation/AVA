import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createContentFlush, createThinkingFlush, createToolFlush } from './flush-timers'
import type { AgentSignals, SessionBridge } from './types'

// ============================================================================
// Mock Factories
// ============================================================================

function mockSignals(): AgentSignals {
  return {
    isRunning: vi.fn(() => false),
    setIsRunning: vi.fn(),
    isPlanMode: vi.fn(() => false),
    setIsPlanMode: vi.fn(),
    currentTurn: vi.fn(() => 0),
    setCurrentTurn: vi.fn(),
    tokensUsed: vi.fn(() => 0),
    setTokensUsed: vi.fn(),
    currentThought: vi.fn(() => ''),
    setCurrentThought: vi.fn(),
    toolActivity: vi.fn(() => []),
    setToolActivity: vi.fn(),
    pendingApproval: vi.fn(() => null),
    setPendingApproval: vi.fn(),
    doomLoopDetected: vi.fn(() => false),
    setDoomLoopDetected: vi.fn(),
    lastError: vi.fn(() => null),
    setLastError: vi.fn(),
    currentAgentId: vi.fn(() => null),
    setCurrentAgentId: vi.fn(),
    activeToolCalls: vi.fn(() => []),
    setActiveToolCalls: vi.fn(),
    streamingContent: vi.fn(() => ''),
    setStreamingContent: vi.fn(),
    streamingTokenEstimate: vi.fn(() => 0),
    setStreamingTokenEstimate: vi.fn(),
    streamingStartedAt: vi.fn(() => null),
    setStreamingStartedAt: vi.fn(),
    error: vi.fn(() => null),
    setError: vi.fn(),
    messageQueue: vi.fn(() => []),
    setMessageQueue: vi.fn(),
  }
}

function mockSession(): SessionBridge {
  return {
    messages: vi.fn(() => []),
    currentSession: vi.fn(() => ({ id: 's-1' })),
    addMessage: vi.fn(),
    updateMessage: vi.fn(),
    updateMessageContent: vi.fn(),
    setMessageError: vi.fn(),
    deleteMessage: vi.fn(),
    deleteMessagesAfter: vi.fn(),
    addFileOperation: vi.fn(),
    selectedModel: vi.fn(() => 'test'),
    setRetryingMessageId: vi.fn(),
    stopEditing: vi.fn(),
    createNewSession: vi.fn(),
    renameSession: vi.fn(),
  }
}

// ============================================================================
// createContentFlush
// ============================================================================

describe('createContentFlush', () => {
  beforeEach(() => {
    vi.useFakeTimers()
  })
  afterEach(() => {
    vi.useRealTimers()
  })

  it('schedule() does not write immediately', () => {
    const signals = mockSignals()
    const content = 'hello'
    const flush = createContentFlush(() => content, signals)

    flush.schedule()

    expect(signals.setStreamingContent).not.toHaveBeenCalled()
  })

  it('schedule() writes after 16ms timer fires', () => {
    const signals = mockSignals()
    const content = 'hello'
    const flush = createContentFlush(() => content, signals)

    flush.schedule()
    vi.advanceTimersByTime(16)

    expect(signals.setStreamingContent).toHaveBeenCalledWith('hello')
    expect(signals.setStreamingTokenEstimate).toHaveBeenCalledWith(2) // ceil(5/4)
  })

  it('multiple schedule() calls coalesce into single write', () => {
    const signals = mockSignals()
    let content = 'a'
    const flush = createContentFlush(() => content, signals)

    flush.schedule()
    content = 'ab'
    flush.schedule()
    content = 'abc'
    flush.schedule()
    vi.advanceTimersByTime(16)

    // Only one write, with latest content
    expect(signals.setStreamingContent).toHaveBeenCalledTimes(1)
    expect(signals.setStreamingContent).toHaveBeenCalledWith('abc')
  })

  it('flush() writes immediately and clears pending timer', () => {
    const signals = mockSignals()
    const content = 'data'
    const flush = createContentFlush(() => content, signals)

    flush.schedule()
    flush.flush()

    expect(signals.setStreamingContent).toHaveBeenCalledWith('data')

    // Advancing time should not trigger a second write
    vi.advanceTimersByTime(20)
    expect(signals.setStreamingContent).toHaveBeenCalledTimes(1)
  })

  it('cleanup() prevents pending timer from firing', () => {
    const signals = mockSignals()
    const flush = createContentFlush(() => 'x', signals)

    flush.schedule()
    flush.cleanup()
    vi.advanceTimersByTime(20)

    expect(signals.setStreamingContent).not.toHaveBeenCalled()
  })
})

// ============================================================================
// createToolFlush
// ============================================================================

describe('createToolFlush', () => {
  beforeEach(() => {
    vi.useFakeTimers()
  })
  afterEach(() => {
    vi.useRealTimers()
  })

  it('scheduleThrottled() does not flush immediately', () => {
    const signals = mockSignals()
    const session = mockSession()
    const tools = [{ id: 't1', name: 'bash', args: {}, status: 'running' as const, startedAt: 0 }]
    const flush = createToolFlush(() => tools, signals, session, 'msg-1')

    flush.scheduleThrottled()

    expect(signals.setActiveToolCalls).not.toHaveBeenCalled()
  })

  it('scheduleThrottled() writes signal-only after 150ms', () => {
    const signals = mockSignals()
    const session = mockSession()
    const tools = [{ id: 't1', name: 'bash', args: {}, status: 'running' as const, startedAt: 0 }]
    const flush = createToolFlush(() => tools, signals, session, 'msg-1')

    flush.scheduleThrottled()
    vi.advanceTimersByTime(150)

    expect(signals.setActiveToolCalls).toHaveBeenCalledTimes(1)
    // Throttled flush should NOT sync to store
    expect(session.updateMessage).not.toHaveBeenCalled()
  })

  it('immediate() flushes to both signal and store', () => {
    const signals = mockSignals()
    const session = mockSession()
    const tools = [{ id: 't1', name: 'bash', args: {}, status: 'success' as const, startedAt: 0 }]
    const flush = createToolFlush(() => tools, signals, session, 'msg-1')

    flush.scheduleThrottled() // set pending
    flush.immediate()

    expect(signals.setActiveToolCalls).toHaveBeenCalledTimes(1)
    expect(session.updateMessage).toHaveBeenCalledWith('msg-1', {
      toolCalls: expect.any(Array),
    })
  })

  it('immediate() cancels pending throttled timer', () => {
    const signals = mockSignals()
    const session = mockSession()
    const tools = [{ id: 't1', name: 'bash', args: {}, status: 'running' as const, startedAt: 0 }]
    const flush = createToolFlush(() => tools, signals, session, 'msg-1')

    flush.scheduleThrottled()
    flush.immediate()
    vi.advanceTimersByTime(200)

    // Only the immediate call, not the throttled one
    expect(signals.setActiveToolCalls).toHaveBeenCalledTimes(1)
  })

  it('cleanup() prevents pending timer from firing', () => {
    const signals = mockSignals()
    const session = mockSession()
    const flush = createToolFlush(() => [], signals, session, 'msg-1')

    flush.scheduleThrottled()
    flush.cleanup()
    vi.advanceTimersByTime(200)

    expect(signals.setActiveToolCalls).not.toHaveBeenCalled()
  })
})

// ============================================================================
// createThinkingFlush
// ============================================================================

describe('createThinkingFlush', () => {
  beforeEach(() => {
    vi.useFakeTimers()
  })
  afterEach(() => {
    vi.useRealTimers()
  })

  it('append() accumulates chunks', () => {
    const session = mockSession()
    const flush = createThinkingFlush(session)

    flush.append('hello ')
    flush.append('world')

    expect(flush.accumulated).toBe('hello world')
  })

  it('first non-empty schedule() writes immediately', () => {
    const session = mockSession()
    const flush = createThinkingFlush(session)

    flush.append('thinking...')
    flush.schedule('msg-1')

    expect(session.updateMessage).toHaveBeenCalledWith('msg-1', {
      metadata: { thinking: 'thinking...' },
    })
  })

  it('subsequent schedule() calls are throttled at 150ms', () => {
    const session = mockSession()
    const flush = createThinkingFlush(session)

    flush.append('first')
    flush.schedule('msg-1')
    ;(session.updateMessage as ReturnType<typeof vi.fn>).mockClear()

    flush.append(' more')
    flush.schedule('msg-1')

    // Not written yet (throttled)
    expect(session.updateMessage).not.toHaveBeenCalled()

    vi.advanceTimersByTime(150)
    expect(session.updateMessage).toHaveBeenCalledWith('msg-1', {
      metadata: { thinking: 'first more' },
    })
  })

  it('finalize() writes remaining content even if not scheduled', () => {
    const session = mockSession()
    const flush = createThinkingFlush(session)

    flush.append('final thought')
    flush.finalize('msg-1')

    expect(session.updateMessage).toHaveBeenCalledWith('msg-1', {
      metadata: { thinking: 'final thought' },
    })
  })

  it('finalize() does not double-write if already flushed', () => {
    const session = mockSession()
    const flush = createThinkingFlush(session)

    flush.append('text')
    flush.schedule('msg-1') // immediate first write

    flush.finalize('msg-1')
    // schedule wrote it, finalize should not re-write identical content
    expect(session.updateMessage).toHaveBeenCalledTimes(1)
  })

  it('finalize() clears pending timer', () => {
    const session = mockSession()
    const flush = createThinkingFlush(session)

    flush.append('a')
    flush.schedule('msg-1') // immediate first
    ;(session.updateMessage as ReturnType<typeof vi.fn>).mockClear()

    flush.append('b')
    flush.schedule('msg-1') // starts throttled timer
    flush.finalize('msg-1') // clears timer, writes latest

    expect(session.updateMessage).toHaveBeenCalledTimes(1)
    expect(session.updateMessage).toHaveBeenCalledWith('msg-1', {
      metadata: { thinking: 'ab' },
    })

    // Timer should not fire
    vi.advanceTimersByTime(200)
    expect(session.updateMessage).toHaveBeenCalledTimes(1)
  })

  it('cleanup() prevents pending timer from firing', () => {
    const session = mockSession()
    const flush = createThinkingFlush(session)

    flush.append('chunk')
    flush.schedule('msg-1') // immediate first
    ;(session.updateMessage as ReturnType<typeof vi.fn>).mockClear()

    flush.append(' more')
    flush.schedule('msg-1')
    flush.cleanup()
    vi.advanceTimersByTime(200)

    expect(session.updateMessage).not.toHaveBeenCalled()
  })
})
