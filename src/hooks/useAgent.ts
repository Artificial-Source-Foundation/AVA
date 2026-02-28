/**
 * useAgent Hook
 * Integrates AgentExecutor from @ava/core-v2 with SolidJS reactive state
 *
 * Provides:
 * - Full agent loop with hooks, doom loop detection, recovery
 * - Reactive tool activity tracking
 * - Event streaming for UI updates
 */

import type { AgentConfig, AgentEvent, AgentInputs, AgentResult } from '@ava/core-v2/agent'
import { runAgent } from '@ava/core-v2/agent'
import { getAgentModes } from '@ava/core-v2/extensions'
import { batch, createSignal } from 'solid-js'
import { checkAutoApproval as sharedCheckAutoApproval } from '../lib/tool-approval'
import { saveMessage, updateMessage } from '../services/database'
import { resolveProvider } from '../services/llm/bridge'
import { logError } from '../services/logger'
import { notifyCompletion } from '../services/notifications'
import {
  pendingApproval as pendingApprovalSignal,
  resolveApproval as resolveApprovalBridge,
} from '../services/tool-approval-bridge'
import { useProject } from '../stores/project'
import { useSession } from '../stores/session'
import { useSettings } from '../stores/settings'
import { useTeam } from '../stores/team'
import type { Message } from '../types'
import type { AgentState, ApprovalRequest, ToolActivity } from './agent'
import { createAgentEventHandler, createTeamBridge } from './agent'

// Re-export types so existing consumers continue working
export type { AgentState, ApprovalRequest, ToolActivity }

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

function createAgentStore() {
  // Signals for reactive state
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

  const abortRef = { current: null as AbortController | null }
  const session = useSession()
  const settingsRef = useSettings()
  const teamStore = useTeam()
  const { currentProject } = useProject()

  // Wire up team bridge + event handler
  const { bridgeToTeam } = createTeamBridge(teamStore)
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

  // ========================================================================
  // Message Helpers
  // ========================================================================

  async function createUserMessage(sessionId: string, content: string): Promise<Message> {
    const msg = await saveMessage({ sessionId, role: 'user', content })
    session.addMessage(msg)
    return msg
  }

  async function createAssistantMessage(sessionId: string): Promise<Message> {
    const msg = await saveMessage({ sessionId, role: 'assistant', content: '' })
    session.addMessage(msg)
    return msg
  }

  // ========================================================================
  // Public API
  // ========================================================================

  async function run(goal: string, config?: Partial<AgentConfig>): Promise<AgentResult | null> {
    if (isRunning()) return null

    if (!currentProject()) {
      setLastError('Open a project before running agent mode.')
      return null
    }

    batch(() => {
      setIsRunning(true)
      setCurrentThought('')
      setLastError(null)
      setDoomLoopDetected(false)
    })

    teamStore.clearTeam()
    abortRef.current = new AbortController()

    try {
      let sessionId = session.currentSession()?.id
      if (!sessionId) {
        const newSession = await session.createNewSession()
        sessionId = newSession.id
      }

      await createUserMessage(sessionId, goal)
      const assistantMsg = await createAssistantMessage(sessionId)

      const inputs: AgentInputs = {
        goal,
        cwd: currentProject()?.directory || '.',
        context: undefined,
      }

      const limits = settingsRef.settings().agentLimits
      const selectedModel = session.selectedModel()
      const agentConfig: Partial<AgentConfig> = {
        provider: resolveProvider(selectedModel),
        model: selectedModel,
        maxTurns: limits.agentMaxTurns,
        maxTimeMinutes: limits.agentMaxTimeMinutes,
        toolMode: getAgentModes().has('praxis')
          ? 'praxis'
          : getAgentModes().has('team')
            ? 'team'
            : undefined,
        ...config,
      }

      let accumulatedContent = ''

      const eventHandler = (event: AgentEvent) => {
        handleAgentEvent(event)

        if (event.type === 'thought') {
          accumulatedContent += event.content
          session.updateMessageContent(assistantMsg.id, accumulatedContent)
        }

        if (event.type === 'agent:finish') {
          const finalContent = event.result.output || accumulatedContent
          const totalTokens = event.result.tokensUsed.input + event.result.tokensUsed.output
          updateMessage(assistantMsg.id, {
            content: finalContent,
            tokensUsed: totalTokens,
          })
          session.updateMessage(assistantMsg.id, {
            content: finalContent,
            tokensUsed: totalTokens,
          })

          void notifyCompletion(
            event.result.success ? 'Agent complete' : 'Agent failed',
            (event.result.output ?? goal).slice(0, 100),
            settingsRef.settings().notifications
          )
        }
      }

      // core-v2: runAgent(inputs, config, signal, onEvent)
      return await runAgent(inputs, agentConfig, abortRef.current.signal, eventHandler)
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err)
      logError(
        'Agent',
        `Agent run failed: ${errorMsg}`,
        err instanceof Error ? err.stack : undefined
      )
      setLastError(errorMsg)
      return null
    } finally {
      setIsRunning(false)
      abortRef.current = null
    }
  }

  function cancel(): void {
    abortRef.current?.abort()
    setIsRunning(false)
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
    // Use the bridge to resolve the middleware-based approval
    resolveApprovalBridge(approved)
    // Also resolve local approval if any
    const request = pendingApproval()
    if (request) {
      request.resolve(approved)
      setPendingApproval(null)
    }
  }

  function clearError(): void {
    setLastError(null)
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

  return {
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
    run,
    cancel,
    togglePlanMode,
    checkAutoApproval,
    resolveApproval,
    clearError,
    getState,
  }
}
