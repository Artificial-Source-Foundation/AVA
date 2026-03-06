/**
 * useAgent Hook — Unified Agent + Chat (Orchestrator)
 *
 * Single hook that drives ALL agent interactions in the desktop app.
 * Delegates to extracted modules:
 *   - tool-execution.ts: diff capture middleware, file path helpers, constants
 *   - streaming.ts: AgentExecutor creation, event routing, flush timers
 *   - turn-manager.ts: run/cancel/steer/retry/edit/regenerate/undo lifecycle
 *
 * Replaces the old split between useAgent (agent mode) and useChat (chat mode).
 * useChat.ts is now a thin backward-compat wrapper over this hook.
 */

import type { AgentExecutor } from '@ava/core-v2/agent'
import { batch, createSignal } from 'solid-js'
import { checkAutoApproval as sharedCheckAutoApproval } from '../lib/tool-approval'
import {
  pendingApproval as pendingApprovalSignal,
  resolveApproval as resolveApprovalBridge,
} from '../services/tool-approval-bridge'
import { useProject } from '../stores/project'
import { useSession } from '../stores/session'
import { useSettings } from '../stores/settings'
import { useTeam } from '../stores/team'
import type { ToolCall } from '../types'
import type { StreamError } from '../types/llm'
import type { AgentState, ApprovalRequest, ToolActivity } from './agent'
import { createAgentEventHandler, createTeamBridge } from './agent'
import { createTurnManager } from './agent/turn-manager'
import type { QueuedMessage } from './chat/types'

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
  // ── Agent signals ─────────────────────────────────────────────────────
  const [isRunning, setIsRunning] = createSignal(false)
  const [isPlanMode, setIsPlanMode] = createSignal(false)
  const [currentTurn, setCurrentTurn] = createSignal(0)
  const [tokensUsed, setTokensUsed] = createSignal(0)
  const [currentThought, setCurrentThought] = createSignal('')
  const [toolActivity, setToolActivity] = createSignal<ToolActivity[]>([])
  const [pendingApproval, setPendingApproval] = createSignal<ApprovalRequest | null>(null)
  const [doomLoopDetected, setDoomLoopDetected] = createSignal(false)
  const [lastError, setLastError] = createSignal<string | null>(null)
  const [currentAgentId, setCurrentAgentId] = createSignal<string | null>(null)

  // ── Chat signals (absorbed from useChat) ──────────────────────────────
  const [activeToolCalls, setActiveToolCalls] = createSignal<ToolCall[]>([])
  const [streamingTokenEstimate, setStreamingTokenEstimate] = createSignal(0)
  const [streamingStartedAt, setStreamingStartedAt] = createSignal<number | null>(null)
  const [error, setError] = createSignal<StreamError | null>(null)
  const [messageQueue, setMessageQueue] = createSignal<QueuedMessage[]>([])
  // Live streaming content — updated on every token WITHOUT touching the session store.
  const [streamingContent, setStreamingContent] = createSignal('')

  // ── External stores / refs ────────────────────────────────────────────
  const abortRef = { current: null as AbortController | null }
  const executorRef = { current: null as AgentExecutor | null }
  const sessionStore = useSession()
  const settingsRef = useSettings()
  const teamStore = useTeam()
  const { currentProject } = useProject()

  // ── Team bridge + event handler ───────────────────────────────────────
  const isTeamMode = () => {
    const gen = settingsRef.settings().generation
    return gen.delegationEnabled === true
  }
  const {
    bridgeToTeam,
    stopAgent,
    sendMessage: sendTeamMessage,
  } = createTeamBridge(teamStore, isTeamMode)
  const handleAgentEvent = createAgentEventHandler(
    {
      setCurrentAgentId,
      setCurrentTurn,
      setTokensUsed,
      setToolActivity,
      setDoomLoopDetected,
      setLastError,
      setIsRunning,
      setCurrentThought,
    },
    bridgeToTeam
  )

  // ── Assemble signals, refs, and bridges for extracted modules ─────────
  const signals = {
    isRunning,
    setIsRunning,
    isPlanMode,
    setIsPlanMode,
    currentTurn,
    setCurrentTurn,
    tokensUsed,
    setTokensUsed,
    currentThought,
    setCurrentThought,
    toolActivity,
    setToolActivity,
    pendingApproval,
    setPendingApproval,
    doomLoopDetected,
    setDoomLoopDetected,
    lastError,
    setLastError,
    currentAgentId,
    setCurrentAgentId,
    activeToolCalls,
    setActiveToolCalls,
    streamingContent,
    setStreamingContent,
    streamingTokenEstimate,
    setStreamingTokenEstimate,
    streamingStartedAt,
    setStreamingStartedAt,
    error,
    setError,
    messageQueue,
    setMessageQueue,
  }

  const refs = { abortRef, executorRef }

  const configDeps = {
    currentProjectDir: () => currentProject()?.directory,
    settingsRef,
  }

  // ── Turn manager (run, cancel, steer, retry, etc.) ────────────────────
  const turnManager = createTurnManager({
    signals,
    refs,
    session: sessionStore,
    handleAgentEvent,
    configDeps,
    teamStore,
  })

  // ====================================================================
  // Local Helpers (approval, plan mode, error, state)
  // ====================================================================

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
      setLastError(null)
      setError(null)
    })
  }

  function getState(): AgentState {
    return {
      isRunning: isRunning(),
      isPlanMode: isPlanMode(),
      currentTurn: currentTurn(),
      tokensUsed: tokensUsed(),
      currentThought: currentThought(),
      toolActivity: toolActivity(),
      pendingApproval: pendingApprovalSignal() as ApprovalRequest | null,
      doomLoopDetected: doomLoopDetected(),
      lastError: lastError(),
    }
  }

  // ====================================================================
  // Return full public API (identical shape to original)
  // ====================================================================

  return {
    // ── Agent signals ─────────────────────────────────────────────────
    isRunning,
    isPlanMode,
    currentTurn,
    tokensUsed,
    currentThought,
    toolActivity,
    pendingApproval: pendingApprovalSignal as () => ApprovalRequest | null,
    doomLoopDetected,
    lastError,
    currentAgentId,

    // ── Chat signals (from absorbed useChat) ──────────────────────────
    isStreaming: isRunning, // alias for backward compat
    activeToolCalls,
    streamingContent,
    streamingTokenEstimate,
    streamingStartedAt,
    error,
    messageQueue,
    queuedCount: () => messageQueue().length,

    // ── Actions (delegated to turn manager) ──────────────────────────
    run: turnManager.run,
    cancel: turnManager.cancel,
    steer: turnManager.steer,
    retryMessage: turnManager.retryMessage,
    editAndResend: turnManager.editAndResend,
    regenerateResponse: turnManager.regenerateResponse,
    undoLastEdit: turnManager.undoLastEdit,

    // ── Queue (delegated to turn manager) ─────────────────────────────
    removeFromQueue: turnManager.removeFromQueue,
    clearQueue: turnManager.clearQueue,

    // ── Agent-specific ────────────────────────────────────────────────
    togglePlanMode,
    checkAutoApproval,
    resolveApproval,
    clearError,
    getState,
    stopAgent,
    sendTeamMessage,
  }
}
