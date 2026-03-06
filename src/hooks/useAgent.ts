/**
 * useAgent Hook — Unified Agent + Chat
 *
 * Single hook that drives ALL agent interactions in the desktop app.
 * Uses AgentExecutor directly, builds proper conversation history with
 * tool_result blocks, and waits for instructions before building prompts.
 *
 * Replaces the old split between useAgent (agent mode) and useChat (chat mode).
 * useChat.ts is now a thin backward-compat wrapper over this hook.
 */

import type { AgentConfig, AgentEvent, AgentResult } from '@ava/core-v2/agent'
import { AgentExecutor, generateTitle } from '@ava/core-v2/agent'
import type { ToolMiddleware, ToolMiddlewareContext } from '@ava/core-v2/extensions'
import { addToolMiddleware, getAgentModes } from '@ava/core-v2/extensions'
import { getPlatform } from '@ava/core-v2/platform'
import { batch, createSignal } from 'solid-js'
import { DEFAULTS } from '../config/constants'
import { estimateCost } from '../lib/cost'
import { checkAutoApproval as sharedCheckAutoApproval } from '../lib/tool-approval'
import { isOAuthSupported } from '../services/auth/oauth'
import { getCoreBudget } from '../services/core-bridge'
import { saveMessage, updateMessage } from '../services/database'
import { readFileContent } from '../services/file-browser'
import { recordFileChange } from '../services/file-versions'
import { resolveProvider } from '../services/llm/bridge'
import { flushLogs, logError, logInfo } from '../services/logger'
import { notifyCompletion } from '../services/notifications'
import {
  pendingApproval as pendingApprovalSignal,
  resolveApproval as resolveApprovalBridge,
} from '../services/tool-approval-bridge'
import { useProject } from '../stores/project'
import { useSession } from '../stores/session'
import { useSettings } from '../stores/settings'
import { useTeam } from '../stores/team'
import type { FileOperationType, Message, ToolCall } from '../types'
import type { StreamError } from '../types/llm'
import type { AgentState, ApprovalRequest, ToolActivity } from './agent'
import { createAgentEventHandler, createTeamBridge } from './agent'
import { buildConversationHistory } from './chat/history-builder'
import { buildSystemPromptAfterInstructions } from './chat/prompt-builder'
import { getModifiedFilePath } from './chat/tool-execution'
import type { QueuedMessage } from './chat/types'

// Re-export types so existing consumers continue working
export type { AgentState, ApprovalRequest, ToolActivity }
export type { QueuedMessage }

// ============================================================================
// Constants
// ============================================================================

/** Tools that modify files and should have diffs captured */
const DIFF_TOOLS = new Set([
  'write_file',
  'create_file',
  'edit',
  'delete_file',
  'delete',
  'multiedit',
])

/** Max file size to capture for diff (500KB) */
const MAX_CAPTURE = 500_000

/** Tools excluded in solo mode to save ~2,500 tokens/turn */
const SOLO_EXCLUDED = new Set([
  // LSP — not wired in desktop yet
  'lsp_diagnostics',
  'lsp_hover',
  'lsp_definition',
  'lsp_references',
  'lsp_document_symbols',
  'lsp_workspace_symbols',
  'lsp_code_actions',
  'lsp_rename',
  'lsp_completions',
  // Delegation/subagent
  'task',
  'sandbox_run',
  // Meta-tools rarely used autonomously
  // Redundant with core tools or rarely needed
  'pty',
  'batch',
  'multiedit',
  'apply_patch',
  // Memory — adds 4 tool definitions
  'memory_read',
  'memory_write',
  'memory_list',
  'memory_delete',
  // Session management
  'plan_enter',
  'plan_exit',
  'recall',
])

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
  // This prevents <For> from destroying/recreating MessageRow DOM on each token.
  const [streamingContent, setStreamingContent] = createSignal('')

  // ── External stores / refs ────────────────────────────────────────────
  const abortRef = { current: null as AbortController | null }
  const executorRef = { current: null as AgentExecutor | null }
  const session = useSession()
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

  // ====================================================================
  // Message Helpers
  // ====================================================================

  async function createUserMessage(
    sessionId: string,
    content: string,
    images?: QueuedMessage['images']
  ): Promise<Message> {
    const msg = await saveMessage({
      sessionId,
      role: 'user',
      content,
      metadata: images?.length ? { images } : undefined,
    })
    session.addMessage(msg)
    return msg
  }

  async function createAssistantMessage(sessionId: string): Promise<Message> {
    const msg = await saveMessage({ sessionId, role: 'assistant', content: '' })
    session.addMessage(msg)
    return msg
  }

  // ====================================================================
  // Config Builder (shared by run + _regenerate)
  // ====================================================================

  async function buildAgentConfig(
    model: string,
    overrides?: Partial<AgentConfig>
  ): Promise<Partial<AgentConfig>> {
    const limits = settingsRef.settings().agentLimits
    const generation = settingsRef.settings().generation
    const delegationEnabled = generation.delegationEnabled
    const reasoningEffort = generation.reasoningEffort
    const thinking =
      reasoningEffort !== 'off'
        ? {
            enabled: true,
            effort: reasoningEffort as
              | 'none'
              | 'minimal'
              | 'low'
              | 'medium'
              | 'high'
              | 'xhigh'
              | 'max',
          }
        : undefined

    const { getToolDefinitions } = await import('@ava/core-v2/tools')
    const allToolNames = getToolDefinitions().map((t) => t.name)
    const allowedTools = delegationEnabled
      ? undefined
      : allToolNames.filter((n) => !n.startsWith('delegate_') && !SOLO_EXCLUDED.has(n))

    const cwd = currentProject()?.directory || '.'
    const customInstructions = generation.customInstructions
    const systemPrompt = await buildSystemPromptAfterInstructions(model, cwd, customInstructions)

    const provider = resolveProvider(model)
    const toolChoice = 'auto' as const

    return {
      provider,
      model,
      systemPrompt,
      maxTurns: limits.agentMaxTurns,
      maxTimeMinutes: limits.agentMaxTimeMinutes,
      toolChoiceStrategy: toolChoice,
      allowedTools,
      toolMode:
        delegationEnabled && getAgentModes().has('praxis')
          ? 'praxis'
          : delegationEnabled && getAgentModes().has('team')
            ? 'team'
            : undefined,
      thinking,
      ...overrides,
    }
  }

  // ====================================================================
  // Diff Capture Middleware
  // ====================================================================

  function createDiffCaptureMiddleware(sessionId: string): ToolMiddleware {
    const originalContents = new Map<string, string | null>()

    return {
      name: 'chat-diff-capture',
      priority: 25,

      async before(ctx: ToolMiddlewareContext) {
        const filePath = getModifiedFilePath(ctx.toolName, ctx.args)
        if (filePath && DIFF_TOOLS.has(ctx.toolName)) {
          try {
            const content = await readFileContent(filePath)
            originalContents.set(
              filePath,
              content && content.length <= MAX_CAPTURE ? content : null
            )
          } catch {
            originalContents.set(filePath, null)
          }
        }
        return undefined
      },

      async after(ctx: ToolMiddlewareContext, result) {
        if (!result) return undefined

        const filePath = getModifiedFilePath(ctx.toolName, ctx.args)
        if (!filePath || !result.success || !DIFF_TOOLS.has(ctx.toolName)) return undefined

        const originalContent = originalContents.get(filePath) ?? null
        originalContents.delete(filePath)

        let newContent: string | null = null
        if (ctx.toolName === 'delete_file' || ctx.toolName === 'delete') {
          newContent = null
        } else {
          try {
            const content = await readFileContent(filePath)
            newContent = content && content.length <= MAX_CAPTURE ? content : null
          } catch {
            /* file may not exist after failure */
          }
        }

        const opType: FileOperationType =
          ctx.toolName === 'edit' || ctx.toolName === 'apply_patch' || ctx.toolName === 'multiedit'
            ? 'edit'
            : ctx.toolName === 'create_file'
              ? 'write'
              : ctx.toolName === 'delete_file' || ctx.toolName === 'delete'
                ? 'delete'
                : 'write'

        const oldLines = originalContent?.split('\n').length ?? 0
        const newLines = newContent?.split('\n').length ?? 0

        const fileOp = {
          id: `${sessionId}-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`,
          sessionId,
          type: opType,
          filePath,
          timestamp: Date.now(),
          originalContent: originalContent ?? undefined,
          newContent: newContent ?? undefined,
          linesAdded: newLines > oldLines ? newLines - oldLines : 0,
          linesRemoved: oldLines > newLines ? oldLines - newLines : 0,
          isNew: originalContent === null && opType === 'write',
        }
        session.addFileOperation(fileOp)
        recordFileChange(sessionId, fileOp)

        return undefined
      },
    }
  }

  // ====================================================================
  // Core execution flow (shared by run + _regenerate)
  // ====================================================================

  async function _execute(
    goal: string,
    sessionId: string,
    assistantMsg: Message,
    priorMessages: ReturnType<typeof buildConversationHistory>,
    model: string,
    configOverrides?: Partial<AgentConfig>
  ): Promise<AgentResult | null> {
    const allToolCalls: ToolCall[] = []
    let accumulatedContent = ''
    let accumulatedThinking = ''

    // Reset streaming content signal
    setStreamingContent('')

    // Streaming text flush (~60fps) to avoid per-token UI churn
    let contentFlushTimer: ReturnType<typeof setTimeout> | null = null
    let contentFlushPending = false
    const scheduleContentFlush = () => {
      contentFlushPending = true
      if (contentFlushTimer !== null) return
      contentFlushTimer = setTimeout(() => {
        contentFlushTimer = null
        if (!contentFlushPending) return
        contentFlushPending = false
        batch(() => {
          setStreamingContent(accumulatedContent)
          setStreamingTokenEstimate(Math.ceil(accumulatedContent.length / 4))
        })
      }, 16)
    }
    const flushStreamingContent = () => {
      if (contentFlushTimer !== null) {
        clearTimeout(contentFlushTimer)
        contentFlushTimer = null
      }
      contentFlushPending = false
      batch(() => {
        setStreamingContent(accumulatedContent)
        setStreamingTokenEstimate(Math.ceil(accumulatedContent.length / 4))
      })
    }

    // Buffered tool call updates — signal-only during streaming, store on start/finish
    let toolFlushTimer: ReturnType<typeof setTimeout> | null = null
    let toolUpdatePending = false
    const flushToolUpdates = (syncToStore: boolean) => {
      if (!toolUpdatePending) return
      toolUpdatePending = false
      const snapshot = [...allToolCalls]
      if (syncToStore) {
        batch(() => {
          setActiveToolCalls(snapshot)
          session.updateMessage(assistantMsg.id, { toolCalls: snapshot })
        })
      } else {
        batch(() => {
          setActiveToolCalls(snapshot)
        })
      }
    }
    const immediateToolFlush = () => {
      if (toolFlushTimer !== null) {
        clearTimeout(toolFlushTimer)
        toolFlushTimer = null
      }
      toolUpdatePending = true
      flushToolUpdates(true)
    }
    const scheduleToolFlush = () => {
      toolUpdatePending = true
      if (toolFlushTimer !== null) return
      toolFlushTimer = setTimeout(() => {
        toolFlushTimer = null
        flushToolUpdates(false)
      }, 150)
    }

    // Thinking flush
    let thinkingFlushTimer: ReturnType<typeof setTimeout> | null = null
    let lastFlushedThinking = ''

    // Register temporary diff-capture middleware
    const diffMiddleware = createDiffCaptureMiddleware(sessionId)
    const diffDisposable = addToolMiddleware(diffMiddleware)

    try {
      const agentConfig = await buildAgentConfig(model, configOverrides)

      // Diagnostic logging
      const { getToolDefinitions } = await import('@ava/core-v2/tools')
      const tools = getToolDefinitions()
      const sysLen = agentConfig.systemPrompt?.length ?? 0
      const filteredToolCount = agentConfig.allowedTools?.length ?? tools.length
      logInfo('Agent', '═══ AGENT RUN START ═══', {
        model,
        provider: agentConfig.provider,
        toolCount: tools.length,
        filteredToolCount,
        toolChoiceStrategy: agentConfig.toolChoiceStrategy ?? 'auto',
        systemPromptChars: sysLen,
        systemPromptEstTokens: Math.ceil(sysLen / 4),
        priorMessages: priorMessages.length,
        goalChars: goal.length,
        maxTurns: agentConfig.maxTurns,
        permissionMode: settingsRef.settings().permissionMode,
      })
      if (agentConfig.systemPrompt) {
        logInfo('Agent', 'System prompt preview', agentConfig.systemPrompt.slice(0, 500))
      }
      void flushLogs()

      // Create executor (not runAgent — we need the reference for steer)
      const executor = new AgentExecutor(agentConfig, (event: AgentEvent) => {
        // Forward to team bridge + agent state signals
        handleAgentEvent(event)

        switch (event.type) {
          case 'thought': {
            accumulatedContent += event.content
            // Debounced flush to avoid per-token MessageList churn.
            scheduleContentFlush()
            break
          }

          case 'thinking': {
            accumulatedThinking += event.content
            if (lastFlushedThinking === '' && accumulatedThinking !== '') {
              session.updateMessage(assistantMsg.id, {
                metadata: { thinking: accumulatedThinking },
              })
              lastFlushedThinking = accumulatedThinking
              break
            }
            if (thinkingFlushTimer !== null) break
            thinkingFlushTimer = setTimeout(() => {
              if (accumulatedThinking !== lastFlushedThinking) {
                session.updateMessage(assistantMsg.id, {
                  metadata: { thinking: accumulatedThinking },
                })
                lastFlushedThinking = accumulatedThinking
              }
              thinkingFlushTimer = null
            }, 150)
            break
          }

          case 'tool:start': {
            const tc: ToolCall = {
              id: `${event.agentId}-${event.toolName}-${Date.now()}`,
              name: event.toolName,
              args: event.args,
              status: 'running',
              startedAt: Date.now(),
              filePath: getModifiedFilePath(event.toolName, event.args) ?? undefined,
              contentOffset: accumulatedContent.length,
            }
            allToolCalls.push(tc)
            immediateToolFlush()
            break
          }

          case 'tool:finish': {
            const tc = [...allToolCalls]
              .reverse()
              .find((t: ToolCall) => t.name === event.toolName && t.status === 'running')
            if (tc) {
              tc.status = event.success ? 'success' : 'error'
              tc.completedAt = Date.now()
              tc.streamingOutput = undefined
              if (event.output) tc.output = event.output
            }
            immediateToolFlush()
            break
          }

          case 'tool:progress': {
            const tc = [...allToolCalls]
              .reverse()
              .find((t: ToolCall) => t.name === event.toolName && t.status === 'running')
            if (tc) {
              tc.streamingOutput = (tc.streamingOutput ?? '') + event.chunk
              scheduleToolFlush()
            }
            break
          }

          case 'context:compacting': {
            logInfo('Agent', 'Context compacting', {
              tokens: event.estimatedTokens,
              limit: event.contextLimit,
            })
            window.dispatchEvent(
              new CustomEvent('ava:compacted', {
                detail: {
                  removed: event.messagesBefore - event.messagesAfter,
                  tokensSaved: event.estimatedTokens - event.contextLimit * 0.5,
                },
              })
            )
            break
          }

          case 'agent:finish': {
            flushStreamingContent()
            const finalContent = event.result.output || accumulatedContent
            const totalTokens = event.result.tokensUsed.input + event.result.tokensUsed.output
            logInfo('Agent', '═══ AGENT RUN COMPLETE ═══', {
              success: event.result.success,
              turns: event.result.turns,
              tokensUsed: event.result.tokensUsed,
              totalTokens,
              outputChars: finalContent.length,
              terminateMode: event.result.terminateMode,
            })
            void flushLogs()

            // Flush pending thinking timer
            if (thinkingFlushTimer !== null) {
              clearTimeout(thinkingFlushTimer)
              thinkingFlushTimer = null
            }
            if (accumulatedThinking && accumulatedThinking !== lastFlushedThinking) {
              session.updateMessage(assistantMsg.id, {
                metadata: { thinking: accumulatedThinking },
              })
            }

            const inputTokens = event.result.tokensUsed.input
            const outputTokens = event.result.tokensUsed.output
            const provider = resolveProvider(model)
            const isFreeTier = isOAuthSupported(provider as Parameters<typeof isOAuthSupported>[0])
            const cost = isFreeTier
              ? undefined
              : (estimateCost(model, inputTokens, outputTokens) ?? undefined)
            const meta: Record<string, unknown> = { costUSD: cost, model }
            if (allToolCalls.length > 0) meta.toolCalls = allToolCalls
            if (accumulatedThinking) meta.thinking = accumulatedThinking

            // Final flush: write content + metadata to store + DB (single update)
            void updateMessage(assistantMsg.id, {
              content: finalContent,
              tokensUsed: totalTokens,
              metadata: meta,
            })
            session.updateMessage(assistantMsg.id, {
              content: finalContent,
              tokensUsed: totalTokens,
              costUSD: cost,
              model,
              toolCalls: allToolCalls.length > 0 ? [...allToolCalls] : undefined,
            })

            // Clear streaming signals
            batch(() => {
              setStreamingContent('')
              setStreamingTokenEstimate(0)
              setActiveToolCalls([])
            })
            getCoreBudget()?.addMessage(assistantMsg.id, finalContent)

            void notifyCompletion(
              event.result.success ? 'Agent complete' : 'Agent failed',
              (event.result.output ?? goal).slice(0, 100),
              settingsRef.settings().notifications
            )
            break
          }

          case 'error': {
            batch(() => {
              setError({ type: 'unknown', message: event.error })
            })
            session.setMessageError(assistantMsg.id, {
              type: 'unknown',
              message: event.error,
              timestamp: Date.now(),
            })
            break
          }

          default:
            break
        }
      })

      executorRef.current = executor

      const cwd = currentProject()?.directory || '.'
      const result = await executor.run(
        {
          goal,
          cwd,
          messages: priorMessages.length > 0 ? priorMessages : undefined,
        },
        abortRef.current!.signal
      )

      return result
    } finally {
      if (thinkingFlushTimer !== null) clearTimeout(thinkingFlushTimer)
      if (toolFlushTimer !== null) clearTimeout(toolFlushTimer)
      if (contentFlushTimer !== null) clearTimeout(contentFlushTimer)
      diffDisposable.dispose()
      executorRef.current = null
    }
  }

  // ====================================================================
  // Public: run() — primary entry point (new user message)
  // ====================================================================

  async function run(goal: string, config?: Partial<AgentConfig>): Promise<AgentResult | null> {
    if (isRunning()) {
      // Queue if already running
      setMessageQueue((prev) => [...prev, { content: goal }])
      logInfo('Agent', 'Queued message', { queueLength: messageQueue().length + 1 })
      return null
    }

    if (!currentProject()) {
      logError('Agent', 'run() blocked — no project open')
      void flushLogs()
      setLastError('Open a project before running agent mode.')
      return null
    }

    batch(() => {
      setIsRunning(true)
      setCurrentThought('')
      setLastError(null)
      setError(null)
      setDoomLoopDetected(false)
      setActiveToolCalls([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
    })

    teamStore.clearTeam()
    abortRef.current = new AbortController()

    try {
      let sessionId = session.currentSession()?.id
      if (!sessionId) {
        const newSession = await session.createNewSession()
        sessionId = newSession.id
      }

      // Build structured conversation history BEFORE adding new messages
      const priorMessages = buildConversationHistory(session.messages())

      const userMsg = await createUserMessage(sessionId, goal)
      getCoreBudget()?.addMessage(userMsg.id, goal)

      // Auto-title new chats from first user message using AI
      const autoTitleEnabled = settingsRef.settings().behavior.sessionAutoTitle
      const currentSession = session.currentSession()
      const defaultName = DEFAULTS.SESSION_NAME
      const sessionName = currentSession?.name?.trim()
      if (autoTitleEnabled && currentSession?.id === sessionId && sessionName === defaultName) {
        void generateTitle(goal).then((title) => {
          if (title) {
            void session.renameSession(sessionId, title)
          }
        })
      }

      const assistantMsg = await createAssistantMessage(sessionId)
      const model = config?.model || session.selectedModel()
      session.updateMessage(assistantMsg.id, { model })

      return await _execute(goal, sessionId, assistantMsg, priorMessages, model, config)
    } catch (err) {
      if (err instanceof Error && err.name === 'AbortError') {
        logInfo('Agent', 'Run aborted')
        return null
      }
      const errorMsg = err instanceof Error ? err.message : String(err)
      logError('Agent', '═══ AGENT RUN FAILED ═══', {
        error: errorMsg,
        stack: err instanceof Error ? err.stack : undefined,
      })
      void flushLogs()
      batch(() => {
        setLastError(errorMsg)
        setError({ type: 'unknown', message: errorMsg })
      })
      return null
    } finally {
      batch(() => {
        setIsRunning(false)
        setStreamingStartedAt(null)
      })
      abortRef.current = null
      // Process queue
      void processQueue()
    }
  }

  // ====================================================================
  // Internal: _regenerate (shared by retry, edit, regenerate)
  // ====================================================================

  async function _regenerate(excludeIds?: Set<string>): Promise<AgentResult | null> {
    if (isRunning()) return null

    const sessionId = session.currentSession()?.id
    if (!sessionId) return null

    batch(() => {
      setIsRunning(true)
      setCurrentThought('')
      setLastError(null)
      setError(null)
      setDoomLoopDetected(false)
      setActiveToolCalls([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
    })

    abortRef.current = new AbortController()

    try {
      // Find the last user message as the goal
      const msgs = session.messages()
      const lastUserMsg = [...msgs].reverse().find((m) => m.role === 'user')
      const goal = lastUserMsg?.content || 'Continue.'

      // Build history, excluding specified IDs and the last user message (goal carries it)
      const allExcluded = new Set(excludeIds)
      if (lastUserMsg) allExcluded.add(lastUserMsg.id)

      const assistantMsg = await createAssistantMessage(sessionId)
      allExcluded.add(assistantMsg.id)

      const priorMessages = buildConversationHistory(msgs, allExcluded)
      const model = session.selectedModel()
      session.updateMessage(assistantMsg.id, { model })

      return await _execute(goal, sessionId, assistantMsg, priorMessages, model)
    } catch (err) {
      if (err instanceof Error && err.name === 'AbortError') {
        logInfo('Agent', 'Regenerate aborted')
        return null
      }
      const errorMsg = err instanceof Error ? err.message : String(err)
      logError('Agent', 'Regenerate failed', { error: errorMsg })
      void flushLogs()
      batch(() => {
        setLastError(errorMsg)
      })
      return null
    } finally {
      batch(() => {
        setIsRunning(false)
        setStreamingStartedAt(null)
      })
      abortRef.current = null
    }
  }

  // ====================================================================
  // Public: Queue Management
  // ====================================================================

  async function processQueue(): Promise<void> {
    const queue = messageQueue()
    if (queue.length === 0) return
    const next = queue[0]!
    setMessageQueue((prev) => prev.slice(1))
    await run(next.content)
  }

  function cancel(): void {
    abortRef.current?.abort()
    abortRef.current = null
    executorRef.current = null
    batch(() => {
      setMessageQueue([])
      setActiveToolCalls([])
      setIsRunning(false)
      setStreamingStartedAt(null)
    })
    logInfo('Agent', 'Cancel')
  }

  function steer(
    content: string,
    _model?: string,
    _images?: Array<{ data: string; mimeType: string; name?: string }>
  ): void {
    // If executor is running, use its steer method
    if (executorRef.current) {
      executorRef.current.steer(content)
      logInfo('Agent', 'Steer via executor', { content: content.slice(0, 80) })
    } else {
      // Fallback: queue as a priority message
      abortRef.current?.abort()
      batch(() => {
        setMessageQueue([{ content }])
        setIsRunning(false)
        setStreamingStartedAt(null)
      })
      logInfo('Agent', 'Steer via queue', { queued: 1 })
    }
  }

  function clearQueue(): void {
    batch(() => setMessageQueue([]))
  }

  function removeFromQueue(index: number): void {
    batch(() => setMessageQueue((prev) => prev.filter((_, i) => i !== index)))
  }

  // ====================================================================
  // Public: Retry / Edit / Regenerate
  // ====================================================================

  async function retryMessage(assistantMessageId: string): Promise<void> {
    const msgs = session.messages()
    const failedIndex = msgs.findIndex((m) => m.id === assistantMessageId)
    if (failedIndex === -1) return

    const userMsg = msgs
      .slice(0, failedIndex)
      .reverse()
      .find((m) => m.role === 'user')
    if (!userMsg) return

    session.setRetryingMessageId(assistantMessageId)
    session.setMessageError(assistantMessageId, null)
    session.deleteMessage(assistantMessageId)
    logInfo('Agent', 'Retry message', { messageId: assistantMessageId })

    try {
      await _regenerate()
    } finally {
      session.setRetryingMessageId(null)
    }
  }

  async function editAndResend(messageId: string, newContent: string): Promise<void> {
    session.updateMessageContent(messageId, newContent)
    await updateMessage(messageId, {
      content: newContent,
      metadata: { editedAt: Date.now() },
    })

    session.deleteMessagesAfter(messageId)
    session.stopEditing()

    logInfo('Agent', 'Edit and resend', { messageId })
    await _regenerate()
  }

  async function regenerateResponse(assistantMessageId: string): Promise<void> {
    const msgs = session.messages()
    const index = msgs.findIndex((m) => m.id === assistantMessageId)
    if (index === -1) return

    const userMsg = msgs
      .slice(0, index)
      .reverse()
      .find((m) => m.role === 'user')
    if (!userMsg) return

    session.deleteMessage(assistantMessageId)
    await _regenerate()
  }

  // ====================================================================
  // Public: Undo
  // ====================================================================

  async function undoLastEdit(): Promise<{ success: boolean; message: string }> {
    const cwd = currentProject()?.directory
    if (!cwd) return { success: false, message: 'No project directory' }

    const shell = getPlatform().shell
    const gitCheck = await shell.exec('git rev-parse --is-inside-work-tree', { cwd })
    if (gitCheck.exitCode !== 0) return { success: false, message: 'Not a git repository' }

    const log = await shell.exec('git log --oneline -20', { cwd })
    const lines = log.stdout.split('\n').filter(Boolean)
    const avaLine = lines.find((l) => l.includes('[ava]'))
    if (!avaLine) return { success: false, message: 'No AI edit to undo' }

    const sha = avaLine.split(' ')[0]
    const revert = await shell.exec(`git revert --no-edit ${sha}`, { cwd })
    logInfo('Agent', 'Undo last edit', { success: revert.exitCode === 0 })

    return revert.exitCode === 0
      ? { success: true, message: `Reverted last AI edit: ${avaLine}` }
      : { success: false, message: revert.stderr || 'Revert failed' }
  }

  // ====================================================================
  // Public: Misc
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
  // Return full public API
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

    // ── Actions ──────────────────────────────────────────────────────
    run,
    cancel,
    steer,
    retryMessage,
    editAndResend,
    regenerateResponse,
    undoLastEdit,

    // ── Queue ─────────────────────────────────────────────────────────
    removeFromQueue,
    clearQueue,

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
