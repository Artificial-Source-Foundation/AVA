/**
 * useAgentStreaming — Streaming state management for the agent hook.
 *
 * Manages content offset tracking for steering splits and provides
 * derived signals that show only the content relevant to the CURRENT
 * live placeholder (not the entire run's accumulation).
 */

import { type Accessor, createMemo, createSignal } from 'solid-js'

import type { ToolCall } from '../types'
import type { StreamError } from '../types/llm'
import type { ThinkingSegment } from './use-rust-agent'

export interface StreamingOffsets {
  streamingContentOffset: Accessor<number>
  setStreamingContentOffset: (v: number) => void
  toolCallsOffset: Accessor<number>
  setToolCallsOffset: (v: number) => void
  thinkingSegmentsOffset: Accessor<number>
  setThinkingSegmentsOffset: (v: number) => void
}

export interface StreamingDerived {
  liveStreamingContent: Accessor<string>
  liveActiveToolCalls: Accessor<ToolCall[]>
  liveThinkingSegments: Accessor<ThinkingSegment[]>
  error: () => StreamError | null
}

interface RustAgentStreaming {
  streamingContent: Accessor<string>
  activeToolCalls: Accessor<ToolCall[]>
  thinkingSegments: Accessor<ThinkingSegment[]>
  error: Accessor<string | null>
}

export interface AgentStreamingState extends StreamingOffsets, StreamingDerived {}

export function createAgentStreaming(rustAgent: RustAgentStreaming): AgentStreamingState {
  // ── Offset signals ──────────────────────────────────────────────────
  const [streamingContentOffset, setStreamingContentOffset] = createSignal(0)
  const [toolCallsOffset, setToolCallsOffset] = createSignal(0)
  const [thinkingSegmentsOffset, setThinkingSegmentsOffset] = createSignal(0)

  // ── Derived signals (apply steering offsets) ────────────────────────
  const liveStreamingContent = createMemo(() =>
    rustAgent.streamingContent().slice(streamingContentOffset())
  )
  const liveActiveToolCalls = createMemo(() => rustAgent.activeToolCalls().slice(toolCallsOffset()))
  const liveThinkingSegments = createMemo(() =>
    rustAgent.thinkingSegments().slice(thinkingSegmentsOffset())
  )

  // Map Rust agent error signal to StreamError shape
  const error = (): StreamError | null => {
    const msg = rustAgent.error()
    return msg ? { type: 'unknown', message: msg } : null
  }

  return {
    streamingContentOffset,
    setStreamingContentOffset,
    toolCallsOffset,
    setToolCallsOffset,
    thinkingSegmentsOffset,
    setThinkingSegmentsOffset,
    liveStreamingContent,
    liveActiveToolCalls,
    liveThinkingSegments,
    error,
  }
}
