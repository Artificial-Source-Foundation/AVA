/**
 * useAgent Hook
 * Integrates AgentExecutor from @estela/core with SolidJS reactive state
 *
 * Provides:
 * - Full agent loop with hooks, doom loop detection, recovery
 * - Reactive tool activity tracking
 * - Event streaming for UI updates
 */

import {
  type AgentConfig,
  type AgentEvent,
  type AgentInputs,
  type AgentResult,
  runAgent,
  type ToolCallInfo,
} from '@estela/core'
import { batch, createSignal } from 'solid-js'
import {
  type ApprovalRequest,
  checkAutoApproval as sharedCheckAutoApproval,
} from '../lib/tool-approval'
import { saveMessage, updateMessage } from '../services/database'
import { logError, logInfo, logWarn } from '../services/logger'
import { useProject } from '../stores/project'
import { useSession } from '../stores/session'
import { useSettings } from '../stores/settings'
import { useTeam } from '../stores/team'
import type { Message } from '../types'

// Re-export for consumers that import from useAgent
export type { ApprovalRequest }

// ============================================================================
// Types
// ============================================================================

/** Tool activity for UI display */
export interface ToolActivity {
  id: string
  name: string
  args: Record<string, unknown>
  status: 'pending' | 'running' | 'success' | 'error'
  output?: string
  error?: string
  startedAt: number
  completedAt?: number
  durationMs?: number
}

/** Agent state */
export interface AgentState {
  isRunning: boolean
  isPlanMode: boolean
  currentTurn: number
  tokensUsed: number
  currentThought: string
  toolActivity: ToolActivity[]
  pendingApproval: ApprovalRequest | null
  doomLoopDetected: boolean
  lastError: string | null
}

// ============================================================================
// Hook Implementation
// ============================================================================

export function useAgent() {
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

  // ==========================================================================
  // Team Bridge — maps agent events to team hierarchy
  // ==========================================================================

  function bridgeToTeam(event: AgentEvent): void {
    switch (event.type) {
      case 'agent:start': {
        const role = teamStore.agentTypeToRole(event.config?.name ?? 'commander')
        const domain = teamStore.inferDomain(event.goal)
        const name = teamStore.generateName(role, domain)

        teamStore.addMember({
          id: event.agentId,
          name,
          role,
          status: 'working',
          parentId: role === 'team-lead' ? null : (teamStore.teamLead()?.id ?? null),
          domain,
          model: event.config?.model ?? 'unknown',
          task: event.goal,
          toolCalls: [],
          messages: [],
          createdAt: event.timestamp,
        })
        break
      }

      case 'agent:finish':
        teamStore.updateMemberStatus(event.agentId, event.result.success ? 'done' : 'error')
        if (!event.result.success) {
          teamStore.updateMember(event.agentId, { error: event.result.error })
        }
        if (event.result.output) {
          teamStore.updateMember(event.agentId, { result: event.result.output })
        }
        break

      case 'thought':
        teamStore.addMessage(event.agentId, {
          id: `thought-${event.timestamp}`,
          role: 'assistant',
          content: event.text,
          timestamp: event.timestamp,
        })
        break

      case 'tool:start':
        teamStore.addToolCall(event.agentId, {
          id: `${event.toolName}-${event.timestamp}`,
          name: event.toolName,
          status: 'running',
          timestamp: event.timestamp,
        })
        break

      case 'tool:finish':
        teamStore.updateToolCall(event.agentId, `${event.toolName}-${event.timestamp}`, {
          status: 'success',
          durationMs: event.durationMs,
        })
        break

      case 'tool:error':
        // Find the running tool call for this tool name and mark as error
        teamStore.updateToolCall(event.agentId, `${event.toolName}-${event.timestamp}`, {
          status: 'error',
        })
        break
    }
  }

  // ==========================================================================
  // Event Handler
  // ==========================================================================

  function handleAgentEvent(event: AgentEvent): void {
    // Bridge all events to team store for hierarchy visualization
    bridgeToTeam(event)

    switch (event.type) {
      case 'agent:start':
        batch(() => {
          setCurrentAgentId(event.agentId)
          setCurrentTurn(0)
          setTokensUsed(0)
          setToolActivity([])
          setDoomLoopDetected(false)
          setLastError(null)
        })
        break

      case 'agent:finish':
        batch(() => {
          setIsRunning(false)
          if (!event.result.success) {
            setLastError(event.result.error ?? 'Agent failed')
          }
        })
        break

      case 'turn:start':
        setCurrentTurn(event.turn)
        break

      case 'turn:finish':
        // Update tool activity with final results
        if (event.toolCalls) {
          updateToolActivityBatch(event.toolCalls)
        }
        break

      case 'thought':
        setCurrentThought((prev) => prev + event.text)
        break

      case 'tool:start':
        addToolActivity({
          id: `${event.toolName}-${Date.now()}`,
          name: event.toolName,
          args: event.args ?? {},
          status: 'running',
          startedAt: event.timestamp,
        })
        break

      case 'tool:finish':
        updateToolActivity(event.toolName, {
          status: 'success',
          output: event.output,
          completedAt: event.timestamp,
          durationMs: event.durationMs,
        })
        break

      case 'tool:error':
        updateToolActivity(event.toolName, {
          status: 'error',
          error: event.error,
          completedAt: event.timestamp,
        })
        break

      case 'tool:metadata': {
        // Handle streaming metadata updates (e.g., file being written)
        // Could update UI with progress info
        break
      }

      case 'error':
        logError('Agent', `Agent error: ${event.error}`)
        batch(() => {
          setLastError(event.error)
          if (event.error.includes('Doom loop')) {
            setDoomLoopDetected(true)
            logWarn('Agent', 'Doom loop detected')
          }
        })
        break

      case 'recovery:start':
        logInfo('Agent', 'Recovery started')
        break

      case 'recovery:finish':
        logInfo('Agent', 'Recovery finished')
        break
    }
  }

  // ==========================================================================
  // Tool Activity Management
  // ==========================================================================

  function addToolActivity(activity: ToolActivity): void {
    setToolActivity((prev) => [...prev, activity])
  }

  function updateToolActivity(toolName: string, updates: Partial<ToolActivity>): void {
    setToolActivity((prev) =>
      prev.map((a) => (a.name === toolName && a.status === 'running' ? { ...a, ...updates } : a))
    )
  }

  function updateToolActivityBatch(toolCalls: ToolCallInfo[]): void {
    // Sync final tool results from turn finish
    setToolActivity((prev) => {
      const updated = [...prev]
      for (const call of toolCalls) {
        const idx = updated.findIndex((a) => a.name === call.name && a.status === 'running')
        if (idx !== -1) {
          updated[idx] = {
            ...updated[idx],
            status: call.success ? 'success' : 'error',
            output: call.result,
            durationMs: call.durationMs,
            completedAt: Date.now(),
          }
        }
      }
      return updated
    })
  }

  // ==========================================================================
  // Message Helpers
  // ==========================================================================

  async function createUserMessage(sessionId: string, content: string): Promise<Message> {
    const msg = await saveMessage({
      sessionId,
      role: 'user',
      content,
    })
    session.addMessage(msg)
    return msg
  }

  async function createAssistantMessage(sessionId: string): Promise<Message> {
    const msg = await saveMessage({
      sessionId,
      role: 'assistant',
      content: '',
    })
    session.addMessage(msg)
    return msg
  }

  // ==========================================================================
  // Public API
  // ==========================================================================

  /**
   * Run the agent with a goal
   */
  async function run(goal: string, config?: Partial<AgentConfig>): Promise<AgentResult | null> {
    if (isRunning()) return null

    // Reset state
    batch(() => {
      setIsRunning(true)
      setCurrentThought('')
      setLastError(null)
      setDoomLoopDetected(false)
    })

    // Clear team for fresh run
    teamStore.clearTeam()

    abortRef.current = new AbortController()

    try {
      // Ensure session exists
      let sessionId = session.currentSession()?.id
      if (!sessionId) {
        const newSession = await session.createNewSession()
        sessionId = newSession.id
      }

      // Create user message
      await createUserMessage(sessionId, goal)

      // Create assistant placeholder
      const assistantMsg = await createAssistantMessage(sessionId)

      // Build agent inputs
      const inputs: AgentInputs = {
        goal,
        cwd: currentProject()?.directory || '.',
        context: undefined, // Could add project context here
      }

      // Build agent config
      const agentConfig: Partial<AgentConfig> = {
        provider: 'anthropic',
        model: session.selectedModel(),
        maxTurns: 20,
        maxTimeMinutes: 10,
        ...config,
      }

      // Track content for message updates
      let accumulatedContent = ''

      // Custom event handler that also updates messages
      const eventHandler = (event: AgentEvent) => {
        handleAgentEvent(event)

        // Accumulate thoughts into message content
        if (event.type === 'thought') {
          accumulatedContent += event.text
          session.updateMessageContent(assistantMsg.id, accumulatedContent)
        }

        // On finish, save final content
        if (event.type === 'agent:finish') {
          const finalContent = event.result.output || accumulatedContent
          updateMessage(assistantMsg.id, {
            content: finalContent,
            tokensUsed: event.result.tokensUsed,
          })
          session.updateMessage(assistantMsg.id, {
            content: finalContent,
            tokensUsed: event.result.tokensUsed,
          })
        }
      }

      // Run the agent
      const result = await runAgent(inputs, agentConfig, abortRef.current.signal, eventHandler)

      return result
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

  /**
   * Cancel the running agent
   */
  function cancel(): void {
    abortRef.current?.abort()
    setIsRunning(false)
  }

  /**
   * Toggle plan mode
   * Note: Plan mode state is managed locally - core plan mode integration deferred
   */
  function togglePlanMode(): void {
    setIsPlanMode((prev) => !prev)
  }

  /**
   * Check if a tool would be auto-approved
   * Delegates to shared logic with user's "always allow" list from settings.
   */
  function checkAutoApproval(
    toolName: string,
    args: Record<string, unknown>
  ): { approved: boolean; reason?: string } {
    return sharedCheckAutoApproval(toolName, args, settingsRef.isToolAutoApproved)
  }

  /**
   * Resolve a pending approval request
   */
  function resolveApproval(approved: boolean): void {
    const request = pendingApproval()
    if (request) {
      request.resolve(approved)
      setPendingApproval(null)
    }
  }

  /**
   * Clear the last error
   */
  function clearError(): void {
    setLastError(null)
  }

  /**
   * Get current agent state
   */
  function getState(): AgentState {
    return {
      isRunning: isRunning(),
      isPlanMode: isPlanMode(),
      currentTurn: currentTurn(),
      tokensUsed: tokensUsed(),
      currentThought: currentThought(),
      toolActivity: toolActivity(),
      pendingApproval: pendingApproval(),
      doomLoopDetected: doomLoopDetected(),
      lastError: lastError(),
    }
  }

  // ==========================================================================
  // Return Hook API
  // ==========================================================================

  return {
    // State signals
    isRunning,
    isPlanMode,
    currentTurn,
    tokensUsed,
    currentThought,
    toolActivity,
    pendingApproval,
    doomLoopDetected,
    lastError,
    currentAgentId,

    // Actions
    run,
    cancel,
    togglePlanMode,
    checkAutoApproval,
    resolveApproval,
    clearError,
    getState,
  }
}
