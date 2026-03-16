/**
 * useAgent Hook — Unified Agent + Chat (Orchestrator)
 *
 * Single hook that drives ALL agent interactions in the desktop app.
 * Delegates to the Rust backend via useRustAgent() for all execution.
 * The TypeScript layer only manages UI state (approval bridge, plan mode, queuing).
 */

import { batch, createSignal } from 'solid-js'
import { checkAutoApproval as sharedCheckAutoApproval } from '../lib/tool-approval'
import {
  pendingApproval as pendingApprovalSignal,
  resolveApproval as resolveApprovalBridge,
} from '../services/tool-approval-bridge'
import { useSettings } from '../stores/settings'
import type { ToolCall } from '../types'
import type { StreamError } from '../types/llm'
import type { AgentState, ApprovalRequest, ToolActivity } from './agent'
import type { QueuedMessage } from './chat/types'
import { useRustAgent } from './use-rust-agent'

// Re-export types so existing consumers continue working
export type { AgentState, ApprovalRequest, ToolActivity }
export type { QueuedMessage }

// ============================================================================
// Singleton
// ============================================================================

type AgentStore = ReturnType<typeof createAgentStore>
let agentStoreSingleton: AgentStore | null = null

export function useAgent(): AgentStore {
  if (!agentStoreSingleton) {
    agentStoreSingleton = createAgentStore()
  }
  return agentStoreSingleton
}

/** Reset singleton for testing — not for production use */
export function _resetAgentSingleton(): void {
  agentStoreSingleton = null
}

// ============================================================================
// Store Factory
// ============================================================================

function createAgentStore() {
  const rustAgent = useRustAgent()
  const settingsRef = useSettings()

  // ── Frontend-only signals ───────────────────────────────────────────
  const [isPlanMode, setIsPlanMode] = createSignal(false)
  const [currentTurn, setCurrentTurn] = createSignal(0)
  const [tokensUsed, setTokensUsed] = createSignal(0)
  const [currentThought, setCurrentThought] = createSignal('')
  const [toolActivity, setToolActivity] = createSignal<ToolActivity[]>([])
  const [pendingApproval, setPendingApproval] = createSignal<ApprovalRequest | null>(null)
  const [doomLoopDetected, setDoomLoopDetected] = createSignal(false)
  const [currentAgentId, setCurrentAgentId] = createSignal<string | null>(null)
  const [streamingTokenEstimate, setStreamingTokenEstimate] = createSignal(0)
  const [streamingStartedAt, setStreamingStartedAt] = createSignal<number | null>(null)
  const [messageQueue, setMessageQueue] = createSignal<QueuedMessage[]>([])

  // Map Rust agent error signal to StreamError shape
  const error = (): StreamError | null => {
    const msg = rustAgent.error()
    return msg ? { type: 'unknown', message: msg } : null
  }

  // ====================================================================
  // Actions
  // ====================================================================

  async function run(goal: string): Promise<unknown> {
    if (rustAgent.isRunning()) {
      setMessageQueue((prev) => [...prev, { content: goal }])
      return null
    }

    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
    })

    try {
      const result = await rustAgent.run(goal)
      return result
    } finally {
      batch(() => {
        setStreamingStartedAt(null)
      })
      // Process queue
      const queue = messageQueue()
      if (queue.length > 0) {
        const next = queue[0]!
        setMessageQueue((prev) => prev.slice(1))
        void run(next.content)
      }
    }
  }

  function cancel(): void {
    void rustAgent.cancel()
    batch(() => {
      setMessageQueue([])
      setStreamingStartedAt(null)
    })
  }

  function steer(content: string): void {
    // In Rust backend mode, steer by cancelling and re-running with new content
    void rustAgent.cancel()
    batch(() => {
      setMessageQueue([{ content }])
    })
  }

  function togglePlanMode(): void {
    setIsPlanMode((prev) => !prev)
  }

  function checkAutoApproval(
    toolName: string,
    args: Record<string, unknown>
  ): { approved: boolean; reason?: string } {
    return sharedCheckAutoApproval(toolName, args, settingsRef.isToolAutoApproved)
  }

  function resolveApproval(approved: boolean): void {
    resolveApprovalBridge(approved)
    const request = pendingApproval()
    if (request) {
      request.resolve(approved)
      setPendingApproval(null)
    }
  }

  function clearError(): void {
    batch(() => {
      rustAgent.clearError()
    })
  }

  function getState(): AgentState {
    return {
      isRunning: rustAgent.isRunning(),
      isPlanMode: isPlanMode(),
      currentTurn: currentTurn(),
      tokensUsed: tokensUsed(),
      currentThought: currentThought(),
      toolActivity: toolActivity(),
      pendingApproval: pendingApprovalSignal() as ApprovalRequest | null,
      doomLoopDetected: doomLoopDetected(),
      lastError: rustAgent.error(),
    }
  }

  function removeFromQueue(index: number): void {
    setMessageQueue((prev) => prev.filter((_, i) => i !== index))
  }

  function clearQueue(): void {
    setMessageQueue([])
  }

  // Stub for message actions — these now require Rust backend support
  async function retryMessage(_assistantMessageId: string): Promise<void> {
    // TODO: implement via Rust IPC
  }
  async function editAndResend(_messageId: string, _newContent: string): Promise<void> {
    // TODO: implement via Rust IPC
  }
  async function regenerateResponse(_assistantMessageId: string): Promise<void> {
    // TODO: implement via Rust IPC
  }
  async function undoLastEdit(): Promise<{ success: boolean; message: string }> {
    // TODO: implement via Rust IPC
    return { success: false, message: 'Not yet implemented via Rust backend' }
  }

  // ====================================================================
  // Return full public API (identical shape to original)
  // ====================================================================

  return {
    // ── Agent signals (mapped from Rust agent) ───────────────────────
    isRunning: rustAgent.isRunning,
    isPlanMode,
    currentTurn,
    tokensUsed,
    currentThought,
    toolActivity,
    pendingApproval: pendingApprovalSignal as () => ApprovalRequest | null,
    doomLoopDetected,
    lastError: rustAgent.error,
    currentAgentId,
    eventTimeline: rustAgent.events,

    // ── Chat signals (mapped from Rust agent) ────────────────────────
    isStreaming: rustAgent.isRunning, // alias for backward compat
    activeToolCalls: rustAgent.activeToolCalls,
    streamingContent: rustAgent.streamingContent,
    streamingTokenEstimate,
    streamingStartedAt,
    error,
    messageQueue,
    queuedCount: () => messageQueue().length,

    // ── Actions ──────────────────────────────────────────────────────
    run,
    cancel,
    steer,
    retryMessage,
    editAndResend,
    regenerateResponse,
    undoLastEdit,

    // ── Queue ────────────────────────────────────────────────────────
    removeFromQueue,
    clearQueue,

    // ── Agent-specific ──────────────────────────────────────────────
    togglePlanMode,
    checkAutoApproval,
    resolveApproval,
    clearError,
    getState,
    stopAgent: (_memberId: string) => false,
    sendTeamMessage: (_memberId: string, _message: string) => {},
  }
}
