/**
 * Flush Timers
 *
 * Batched UI update timers for streaming content, tool calls, and thinking.
 * Each factory returns a schedule/flush/cleanup triple so the streaming
 * handler can pump data in and let timers coalesce updates at ~60fps (content)
 * or 150ms (tools/thinking).
 */

import { batch } from 'solid-js'
import type { ToolCall } from '../../types'
import type { AgentSignals, SessionBridge } from './types'

// ============================================================================
// Content Flush (~60fps / 16ms)
// ============================================================================

export interface ContentFlush {
  schedule: () => void
  flush: () => void
  cleanup: () => void
}

/**
 * Creates a content flush timer that coalesces streaming token updates
 * into batched signal writes at ~60fps.
 */
export function createContentFlush(getContent: () => string, signals: AgentSignals): ContentFlush {
  let timer: ReturnType<typeof setTimeout> | null = null
  let pending = false

  const writeBatch = (): void => {
    const content = getContent()
    batch(() => {
      signals.setStreamingContent(content)
      signals.setStreamingTokenEstimate(Math.ceil(content.length / 4))
    })
  }

  return {
    schedule(): void {
      pending = true
      if (timer !== null) return
      timer = setTimeout(() => {
        timer = null
        if (!pending) return
        pending = false
        writeBatch()
      }, 16)
    },
    flush(): void {
      if (timer !== null) {
        clearTimeout(timer)
        timer = null
      }
      pending = false
      writeBatch()
    },
    cleanup(): void {
      if (timer !== null) {
        clearTimeout(timer)
        timer = null
      }
    },
  }
}

// ============================================================================
// Tool Call Flush (150ms)
// ============================================================================

export interface ToolFlush {
  scheduleThrottled: () => void
  immediate: () => void
  cleanup: () => void
}

/**
 * Creates a tool-call flush timer that batches rapid tool updates and
 * syncs to both the signal and session store.
 */
export function createToolFlush(
  getToolCalls: () => ToolCall[],
  signals: AgentSignals,
  session: SessionBridge,
  assistantMsgId: string
): ToolFlush {
  let timer: ReturnType<typeof setTimeout> | null = null
  let pending = false

  const doFlush = (syncToStore: boolean): void => {
    if (!pending) return
    pending = false
    const snapshot = [...getToolCalls()]
    if (syncToStore) {
      batch(() => {
        signals.setActiveToolCalls(snapshot)
        session.updateMessage(assistantMsgId, { toolCalls: snapshot })
      })
    } else {
      batch(() => {
        signals.setActiveToolCalls(snapshot)
      })
    }
  }

  return {
    scheduleThrottled(): void {
      pending = true
      if (timer !== null) return
      timer = setTimeout(() => {
        timer = null
        doFlush(false)
      }, 150)
    },
    immediate(): void {
      if (timer !== null) {
        clearTimeout(timer)
        timer = null
      }
      pending = true
      doFlush(true)
    },
    cleanup(): void {
      if (timer !== null) {
        clearTimeout(timer)
        timer = null
      }
    },
  }
}

// ============================================================================
// Thinking Flush (150ms)
// ============================================================================

export interface ThinkingFlush {
  /** Append a chunk of thinking text. */
  append: (chunk: string) => void
  /** Schedule a throttled write — first non-empty write is immediate. */
  schedule: (assistantMsgId: string) => void
  /** Flush any remaining thinking content and clean up the timer. */
  finalize: (assistantMsgId: string) => void
  cleanup: () => void
  /** Access the accumulated thinking text. */
  readonly accumulated: string
}

/**
 * Creates a thinking flush timer that throttles extended-thinking metadata
 * writes to the session store. First non-empty write is immediate;
 * subsequent writes are throttled at 150ms.
 */
export function createThinkingFlush(session: SessionBridge): ThinkingFlush {
  let _accumulated = ''
  let lastFlushed = ''
  let timer: ReturnType<typeof setTimeout> | null = null

  return {
    append(chunk: string): void {
      _accumulated += chunk
    },
    get accumulated(): string {
      return _accumulated
    },
    schedule(assistantMsgId: string): void {
      // First non-empty write is immediate
      if (lastFlushed === '' && _accumulated !== '') {
        session.updateMessage(assistantMsgId, { metadata: { thinking: _accumulated } })
        lastFlushed = _accumulated
        return
      }
      if (timer !== null) return
      timer = setTimeout(() => {
        if (_accumulated !== lastFlushed) {
          session.updateMessage(assistantMsgId, { metadata: { thinking: _accumulated } })
          lastFlushed = _accumulated
        }
        timer = null
      }, 150)
    },
    finalize(assistantMsgId: string): void {
      if (timer !== null) {
        clearTimeout(timer)
        timer = null
      }
      if (_accumulated && _accumulated !== lastFlushed) {
        session.updateMessage(assistantMsgId, { metadata: { thinking: _accumulated } })
      }
    },
    cleanup(): void {
      if (timer !== null) {
        clearTimeout(timer)
        timer = null
      }
    },
  }
}
