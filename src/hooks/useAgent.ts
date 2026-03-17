/**
 * useAgent Hook — Unified Agent + Chat (Orchestrator)
 *
 * Single hook that drives ALL agent interactions in the desktop app.
 * Delegates to the Rust backend via useRustAgent() for all execution.
 * The TypeScript layer only manages UI state (approval bridge, plan mode, queuing).
 */

import { writeTextFile } from '@tauri-apps/plugin-fs'
import { batch, createEffect, createSignal, on } from 'solid-js'

const DEBUG_LOG = '/tmp/ava-debug/agent.log'
async function debugLog(msg: string): Promise<void> {
  const line = `[${new Date().toISOString()}] ${msg}\n`
  try {
    await writeTextFile(DEBUG_LOG, line, { append: true })
  } catch {
    // fallback: also console.log so we don't lose it
    console.log('[ava-debug]', msg)
  }
}

import { checkAutoApproval as sharedCheckAutoApproval } from '../lib/tool-approval'
import { rustAgent as rustAgentBridge, rustBackend } from '../services/rust-bridge'
import { useSession } from '../stores/session'
import { useSettings } from '../stores/settings'
import type { Message } from '../types'
import type { StreamError } from '../types/llm'
import type { ApprovalRequestEvent } from '../types/rust-ipc'
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
  const session = useSession()

  // ── Frontend-only signals ───────────────────────────────────────────
  const [isPlanMode, setIsPlanMode] = createSignal(false)
  const [currentTurn, _setCurrentTurn] = createSignal(0)
  const [tokensUsed, _setTokensUsed] = createSignal(0)
  const [currentThought, setCurrentThought] = createSignal('')
  const [toolActivity, setToolActivity] = createSignal<ToolActivity[]>([])
  const [pendingApproval, setPendingApproval] = createSignal<ApprovalRequest | null>(null)
  const [doomLoopDetected, setDoomLoopDetected] = createSignal(false)
  const [currentAgentId, _setCurrentAgentId] = createSignal<string | null>(null)
  const [streamingTokenEstimate, setStreamingTokenEstimate] = createSignal(0)
  const [streamingStartedAt, setStreamingStartedAt] = createSignal<number | null>(null)
  const [messageQueue, setMessageQueue] = createSignal<QueuedMessage[]>([])

  // Map Rust agent error signal to StreamError shape
  const error = (): StreamError | null => {
    const msg = rustAgent.error()
    return msg ? { type: 'unknown', message: msg } : null
  }

  // ── Watch for approval_request / question_request events from Rust ──
  let lastProcessedEventIdx = 0

  createEffect(
    on(rustAgent.events, (allEvents) => {
      for (let i = lastProcessedEventIdx; i < allEvents.length; i++) {
        const event = allEvents[i]!
        if (event.type === 'approval_request') {
          const approvalEvent = event as ApprovalRequestEvent
          const riskLevel = (
            ['low', 'medium', 'high', 'critical'].includes(approvalEvent.risk_level)
              ? approvalEvent.risk_level
              : 'medium'
          ) as 'low' | 'medium' | 'high' | 'critical'

          const toolName = approvalEvent.tool_name
          const toolType =
            toolName === 'bash'
              ? ('command' as const)
              : toolName.startsWith('mcp_')
                ? ('mcp' as const)
                : ('file' as const)

          setPendingApproval({
            id: approvalEvent.id,
            type: toolType,
            toolName,
            args: approvalEvent.args as Record<string, unknown>,
            description: approvalEvent.reason,
            riskLevel,
            resolve: () => {}, // not used — resolution goes through IPC
          })
        }
      }
      lastProcessedEventIdx = allEvents.length
    })
  )

  // ====================================================================
  // Actions
  // ====================================================================

  async function run(goal: string, config?: { model?: string }): Promise<unknown> {
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

    // Ensure a session exists before adding messages
    let currentSess = session.currentSession()
    if (!currentSess) {
      await session.createNewSession()
      currentSess = session.currentSession()
    }
    const sessionId = currentSess?.id ?? ''

    // Add user message to the session store so it's visible immediately
    const userMsg: Message = {
      id: `user-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
      sessionId,
      role: 'user',
      content: goal,
      createdAt: Date.now(),
    }
    session.addMessage(userMsg)
    void debugLog(`addMessage done, count=${session.messages().length}, sessionId=${sessionId}`)

    try {
      void debugLog(`calling rustAgent.run("${goal.slice(0, 50)}")`)
      const result = await rustAgent.run(goal, { model: config?.model })
      void debugLog(
        `rustAgent.run resolved, result=${JSON.stringify(result)}, error=${rustAgent.error()}`
      )
      void debugLog(`messages count after run: ${session.messages().length}`)
      void debugLog(`streamingContent length: ${rustAgent.streamingContent().length}`)
      void debugLog(`currentSession: ${session.currentSession()?.id ?? 'NULL'}`)

      // Add the assistant response from streamed tokens
      const content = rustAgent.streamingContent()
      if (content) {
        const assistantMsg: Message = {
          id: `asst-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
          sessionId,
          role: 'assistant',
          content,
          createdAt: Date.now(),
          tokensUsed: rustAgent.tokenUsage().output,
          costUSD: rustAgent.tokenUsage().cost,
          toolCalls: rustAgent.activeToolCalls(),
        }
        session.addMessage(assistantMsg)
      }

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
    void rustAgent.steer(content)
  }

  function followUp(content: string): void {
    void rustAgent.followUp(content)
    setMessageQueue((prev) => [...prev, { content }])
  }

  function postComplete(content: string, group?: number): void {
    void rustAgent.postComplete(content, group)
    setMessageQueue((prev) => [...prev, { content }])
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

  function resolveApproval(approved: boolean, alwaysAllow?: boolean): void {
    setPendingApproval(null)
    void rustAgentBridge.resolveApproval(approved, alwaysAllow ?? false).catch((err) => {
      console.error('Failed to resolve approval:', err)
    })
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
      pendingApproval: pendingApproval(),
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

  // Message actions — wired through Rust IPC
  async function retryMessage(_assistantMessageId: string): Promise<void> {
    if (rustAgent.isRunning()) return
    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
    })
    try {
      await rustBackend.retryLastMessage()
    } finally {
      setStreamingStartedAt(null)
    }
  }

  async function editAndResend(messageId: string, newContent: string): Promise<void> {
    if (rustAgent.isRunning()) return
    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
    })
    try {
      await rustBackend.editAndResend({ messageId, newContent })
    } finally {
      setStreamingStartedAt(null)
    }
  }

  async function regenerateResponse(_assistantMessageId: string): Promise<void> {
    if (rustAgent.isRunning()) return
    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
    })
    try {
      await rustBackend.regenerateResponse()
    } finally {
      setStreamingStartedAt(null)
    }
  }

  async function undoLastEdit(): Promise<{ success: boolean; message: string }> {
    const result = await rustBackend.undoLastEdit()
    return { success: result.success, message: result.message }
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
    pendingApproval,
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
    followUp,
    postComplete,
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
