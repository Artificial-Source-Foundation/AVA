import type { Setter } from 'solid-js'
import { batch } from 'solid-js'
import { debugLog } from '../lib/debug-log'
import { log } from '../lib/logger'
import { useLayout } from '../stores/layout'
import { usePlanOverlay } from '../stores/planOverlayStore'
import type { ToolCall } from '../types'
import type {
  AgentEvent,
  BudgetWarningEvent,
  CompleteEvent,
  PlanCreatedEvent,
  PlanData,
  PlanStepCompleteEvent,
  StreamingEditProgressEvent,
  SubagentCompleteEvent,
  SubmitGoalResult,
  TodoItem,
  TodoUpdateEvent,
  ToolCallEvent,
  ToolResultEvent,
} from '../types/rust-ipc'
import type { ThinkingSegment } from './use-rust-agent'

/** Mutable streaming metrics — shared between the event handler and IPC layer. */
export interface StreamingMetrics {
  chunkCount: number
  totalTextLen: number
  runStartTime: number
  firstTokenLogged: boolean
  pendingToolNames: string[]
}

/** Completion resolver for web-mode promise-based flow. */
export interface CompletionResolver {
  resolve: ((result: SubmitGoalResult | null) => void) | null
}

interface EventHandlerDeps {
  metrics: StreamingMetrics
  completion: CompletionResolver
  appendEvent: (event: AgentEvent) => void
  setStreamingContent: Setter<string>
  setThinkingContent: Setter<string>
  setActiveToolCalls: Setter<ToolCall[]>
  setError: Setter<string | null>
  setIsRunning: Setter<boolean>
  setTokenUsage: Setter<{ input: number; output: number; cost: number }>
  setProgressMessage: Setter<string | null>
  setBudgetWarning: Setter<{
    thresholdPercent: number
    currentCostUsd: number
    maxBudgetUsd: number
  } | null>
  setPendingPlan: Setter<PlanData | null>
  setThinkingSegments: Setter<ThinkingSegment[]>
  setTodos: Setter<TodoItem[]>
  isTauriRuntime: boolean
}

/**
 * Create the shared agent event handler.
 * Used by both Tauri listen() and WebSocket event paths.
 */
export function createAgentEventHandler(deps: EventHandlerDeps): (event: AgentEvent) => void {
  const {
    metrics,
    completion,
    appendEvent,
    setStreamingContent,
    setThinkingContent,
    setActiveToolCalls,
    setError,
    setIsRunning,
    setTokenUsage,
    setProgressMessage,
    setBudgetWarning,
    setPendingPlan,
    setThinkingSegments,
    setTodos,
    isTauriRuntime,
  } = deps

  return (event: AgentEvent): void => {
    const timedEvent = {
      ...event,
      timestamp: (event as { timestamp?: number }).timestamp ?? Date.now(),
    } as unknown as AgentEvent
    appendEvent(timedEvent)

    debugLog(
      'event',
      timedEvent.type,
      timedEvent.type === 'token'
        ? `(${(timedEvent.content as string)?.length ?? 0} chars)`
        : timedEvent
    )

    switch (timedEvent.type) {
      case 'token':
        metrics.chunkCount++
        metrics.totalTextLen += (timedEvent.content as string)?.length ?? 0
        if (!metrics.firstTokenLogged && metrics.runStartTime > 0) {
          log.info('perf', 'First token latency', { ttftMs: Date.now() - metrics.runStartTime })
          metrics.firstTokenLogged = true
        }
        setStreamingContent((prev) => prev + timedEvent.content)
        break
      case 'thinking':
        debugLog('thinking', 'received:', (timedEvent.content as string)?.length ?? 0, 'chars')
        setThinkingContent((prev) => prev + timedEvent.content)
        // Track for interleaved segments: accumulate into current block or start new one
        setThinkingSegments((prev) => {
          const last = prev[prev.length - 1]
          if (last && last.toolCallIds.length === 0) {
            // Still accumulating into the same thinking block (no tools have been called yet for this block)
            return [
              ...prev.slice(0, -1),
              { thinking: last.thinking + (timedEvent.content as string), toolCallIds: [] },
            ]
          }
          // New thinking block (after tool calls or at start)
          return [...prev, { thinking: timedEvent.content as string, toolCallIds: [] }]
        })
        break
      case 'tool_call': {
        const toolCallEvent = timedEvent as ToolCallEvent
        log.info('tools', 'Tool call started', { tool: timedEvent.name })
        debugLog(
          'tools',
          'tool_call:',
          (timedEvent as { name?: string }).name,
          toolCallEvent.args ?? toolCallEvent.arguments
        )
        metrics.pendingToolNames.push(timedEvent.name as string)
        const newToolId =
          toolCallEvent.id ?? `${timedEvent.name}-${Date.now()}-${metrics.pendingToolNames.length}`
        // Extract file path from args for file-modifying tools
        const args = (toolCallEvent.args ?? toolCallEvent.arguments ?? {}) as Record<
          string,
          unknown
        >
        const filePath = (args.file_path ?? args.filePath ?? args.path ?? args.output_path) as
          | string
          | undefined
        setActiveToolCalls((prev) => {
          const existingIdx = prev.findIndex((tc) => tc.id === newToolId)
          if (existingIdx >= 0) {
            const updated = [...prev]
            updated[existingIdx] = {
              ...updated[existingIdx]!,
              name: timedEvent.name,
              args,
              filePath: filePath ?? updated[existingIdx]!.filePath,
              status: 'running',
            }
            return updated
          }
          return [
            ...prev,
            {
              id: newToolId,
              name: timedEvent.name,
              args,
              status: 'running' as const,
              startedAt: Date.now(),
              filePath,
            },
          ]
        })
        // Associate this tool call with the current thinking segment
        setThinkingSegments((prev) => {
          if (prev.length === 0) {
            // Tool called before any thinking — add empty thinking segment
            return [{ thinking: '', toolCallIds: [newToolId] }]
          }
          const last = prev[prev.length - 1]!
          return [
            ...prev.slice(0, -1),
            { thinking: last.thinking, toolCallIds: [...last.toolCallIds, newToolId] },
          ]
        })
        break
      }
      case 'tool_result': {
        const toolResultEvent = timedEvent as ToolResultEvent
        const callId = toolResultEvent.call_id ?? toolResultEvent.callId ?? null
        debugLog(
          'tools',
          'tool_result:',
          timedEvent.is_error ? 'ERROR' : 'OK',
          (timedEvent.content as string)?.slice(0, 120)
        )
        // Parse todos from tool_result content if it looks like a todo_write output.
        if (!timedEvent.is_error) {
          const content = (timedEvent.content as string) ?? ''
          if (content.startsWith('Updated todo list')) {
            try {
              const jsonStart = content.indexOf('[')
              if (jsonStart >= 0) {
                const parsed = JSON.parse(content.slice(jsonStart)) as TodoItem[]
                log.info('todo', 'Parsed todos from tool_result', { count: parsed.length })
                setTodos(parsed)
                if (parsed.length > 0) {
                  try {
                    const layout = useLayout()
                    layout.switchRightPanelTab('todos')
                  } catch {
                    // Layout context may not be available
                  }
                }
              }
            } catch {
              debugLog('todo', 'Failed to parse todos from tool_result')
            }
          }
        }
        setActiveToolCalls((prev) => {
          const firstIdx = callId
            ? prev.findIndex((tc) => tc.id === callId)
            : prev.findIndex((tc) => tc.status === 'running')
          if (firstIdx < 0) return prev

          const first = prev[firstIdx]
          const completedAt = Date.now()
          const durationMs = completedAt - first.startedAt
          log.info('tools', 'Tool call completed', {
            tool: first.name,
            success: !timedEvent.is_error,
            durationMs,
          })
          log.debug('perf', 'Tool execution time', { tool: first.name, durationMs })

          // Build diff data for file-modifying tools from their args
          let diff: { oldContent: string; newContent: string } | undefined = first.diff
          if (!timedEvent.is_error) {
            const toolArgs = first.args as Record<string, unknown>
            if (
              first.name === 'edit' ||
              first.name === 'apply_patch' ||
              first.name === 'multiedit'
            ) {
              const oldStr = (toolArgs.old_text ??
                toolArgs.old_string ??
                toolArgs.old_content ??
                '') as string
              const newStr = (toolArgs.new_text ??
                toolArgs.new_string ??
                toolArgs.new_content ??
                '') as string
              if (oldStr || newStr) {
                diff = { oldContent: oldStr, newContent: newStr }
              }
            } else if (
              first.name === 'write' ||
              first.name === 'write_file' ||
              first.name === 'create_file'
            ) {
              const content = (toolArgs.content ?? toolArgs.new_content ?? '') as string
              if (content) {
                diff = { oldContent: '', newContent: content }
              }
            }
          }

          // Create a NEW object so SolidJS For detects the change and re-renders.
          const updated = [...prev]
          updated[firstIdx] = {
            ...first,
            status: timedEvent.is_error ? 'error' : 'success',
            output: toolResultEvent.content,
            completedAt,
            diff,
          }
          return updated
        })
        break
      }
      case 'progress':
        log.debug('agent', 'Progress', { message: timedEvent.message })
        setProgressMessage(timedEvent.message)
        break
      case 'budget_warning': {
        const bw = timedEvent as BudgetWarningEvent
        log.warn('agent', 'Budget warning', {
          threshold: bw.thresholdPercent,
          current: bw.currentCostUsd,
          max: bw.maxBudgetUsd,
        })
        setBudgetWarning({
          thresholdPercent: bw.thresholdPercent,
          currentCostUsd: bw.currentCostUsd,
          maxBudgetUsd: bw.maxBudgetUsd,
        })
        break
      }
      case 'token_usage': {
        const tu = timedEvent as unknown as Record<string, number>
        const input = tu.input_tokens ?? tu.inputTokens ?? 0
        const output = tu.output_tokens ?? tu.outputTokens ?? 0
        const cost = tu.cost_usd ?? tu.costUsd ?? 0
        log.debug('perf', 'Token usage update', { input, output, cost })
        setTokenUsage({ input, output, cost })
        break
      }
      case 'complete':
        debugLog('agent', 'complete event received')
        log.info('streaming', 'Stream completed', {
          chunks: metrics.chunkCount,
          totalChars: metrics.totalTextLen,
          elapsedMs: Date.now() - metrics.runStartTime,
        })
        // In Tauri mode, do NOT set isRunning(false) here — let useAgent.ts
        // finalize the message content first, then call endRun() in a batch
        // to avoid a flash where the message switches from streaming to stored
        // with empty content. In web mode, set it because useAgent.ts waits
        // for the completion promise.
        batch(() => {
          if (!isTauriRuntime) {
            setIsRunning(false)
          }
          // Mark any remaining running tool calls as completed
          setActiveToolCalls((prev) => {
            const updated = [...prev]
            for (const tc of updated) {
              if (tc.status === 'running') {
                tc.status = 'success'
                tc.completedAt = Date.now()
              }
            }
            return updated
          })
        })
        // Resolve the terminal completion promise for both runtimes so the
        // caller can settle as soon as the backend emits a terminal event.
        if (completion.resolve) {
          const completeEvent = timedEvent as CompleteEvent
          completion.resolve({
            success: true,
            turns: 0,
            sessionId: completeEvent.session?.id ?? '',
          })
          completion.resolve = null
        }
        break
      case 'error':
        log.error('agent', 'Agent event error', { message: timedEvent.message })
        log.error('streaming', 'Stream error', {
          message: timedEvent.message,
          chunks: metrics.chunkCount,
          elapsedMs: Date.now() - metrics.runStartTime,
        })
        batch(() => {
          setError(timedEvent.message)
          setIsRunning(false)
        })
        // Resolve the terminal completion promise on error too
        if (completion.resolve) {
          completion.resolve(null)
          completion.resolve = null
        }
        break

      case 'plan_created': {
        debugLog('plan', 'plan_created event', (timedEvent as PlanCreatedEvent).plan?.summary)
        const planEvent = timedEvent as PlanCreatedEvent
        setPendingPlan({ ...planEvent.plan, requestId: planEvent.id })
        break
      }

      case 'plan_step_complete': {
        const stepEvent = timedEvent as PlanStepCompleteEvent
        if (!stepEvent.step_id) {
          log.warn('agent', 'Ignoring malformed plan_step_complete without step_id')
          break
        }
        debugLog('plan', 'plan_step_complete event', stepEvent.step_id)
        usePlanOverlay().markStepComplete(stepEvent.step_id)
        break
      }

      case 'todo_update': {
        const todoEvent = timedEvent as TodoUpdateEvent
        log.info('todo', 'todo_update received', {
          count: todoEvent.todos?.length ?? 0,
          raw: JSON.stringify(todoEvent).slice(0, 200),
        })
        debugLog('todo', 'todo_update event', todoEvent.todos?.length ?? 0, 'items')
        setTodos(todoEvent.todos ?? [])
        // Auto-open the right panel to the Todos tab when todos arrive
        if (todoEvent.todos.length > 0) {
          try {
            const layout = useLayout()
            layout.switchRightPanelTab('todos')
          } catch {
            // Layout context may not be available in all environments
          }
        }
        break
      }

      case 'streaming_edit_progress': {
        const editEvent = timedEvent as StreamingEditProgressEvent
        const callId = editEvent.call_id
        const toolName = editEvent.tool_name
        const bytesReceived = editEvent.bytes_received
        if (!callId || !toolName || typeof bytesReceived !== 'number') {
          log.warn('agent', 'Ignoring malformed streaming_edit_progress event', {
            hasCallId: !!callId,
            hasToolName: !!toolName,
            hasBytesReceived: typeof bytesReceived === 'number',
          })
          break
        }
        const filePath = editEvent.file_path ?? undefined
        const streamingOutput = `Receiving ${toolName}${filePath ? ` for ${filePath}` : ''} (${bytesReceived} bytes)`

        setActiveToolCalls((prev) => {
          const matchIdx = prev.findIndex((tc) => tc.id === callId)

          if (matchIdx >= 0) {
            const updated = [...prev]
            updated[matchIdx] = {
              ...updated[matchIdx]!,
              filePath: filePath ?? updated[matchIdx]!.filePath,
              streamingOutput,
            }
            return updated
          }

          return [
            ...prev,
            {
              id: callId,
              name: toolName,
              args: {},
              status: 'running',
              startedAt: Date.now(),
              filePath,
              streamingOutput,
            },
          ]
        })
        break
      }

      case 'subagent_complete': {
        const subagentEvent = timedEvent as SubagentCompleteEvent
        const callId = subagentEvent.call_id
        const sessionId = subagentEvent.session_id
        const description = subagentEvent.description
        const inputTokens = subagentEvent.input_tokens
        const outputTokens = subagentEvent.output_tokens
        const costUsd = subagentEvent.cost_usd
        if (
          !callId ||
          !sessionId ||
          !description ||
          typeof inputTokens !== 'number' ||
          typeof outputTokens !== 'number' ||
          typeof costUsd !== 'number'
        ) {
          log.warn('agent', 'Ignoring malformed subagent_complete event', {
            hasCallId: !!callId,
            hasSessionId: !!sessionId,
            hasDescription: !!description,
            hasInputTokens: typeof inputTokens === 'number',
            hasOutputTokens: typeof outputTokens === 'number',
            hasCostUsd: typeof costUsd === 'number',
          })
          break
        }
        const agentType = subagentEvent.agent_type ?? undefined
        const summaryLines = [
          description,
          `Session: ${sessionId}`,
          `Tokens: ${inputTokens} in / ${outputTokens} out`,
          `Cost: $${costUsd.toFixed(4)}`,
          ...(agentType ? [`Agent: ${agentType}`] : []),
          ...(subagentEvent.provider ? [`Provider: ${subagentEvent.provider}`] : []),
          ...(subagentEvent.resumed ? ['Resumed existing delegated session'] : []),
        ]

        setActiveToolCalls((prev) => {
          const matchIdx = prev.findIndex((tc) => tc.id === callId)
          const output = summaryLines.join('\n')
          const completedAt = Date.now()

          if (matchIdx >= 0) {
            const updated = [...prev]
            updated[matchIdx] = {
              ...updated[matchIdx]!,
              output,
              status: updated[matchIdx]!.status === 'error' ? updated[matchIdx]!.status : 'success',
              completedAt,
            }
            return updated
          }

          return [
            ...prev,
            {
              id: callId,
              name: 'task',
              args: {
                description,
                ...(agentType ? { agent_type: agentType } : {}),
              },
              status: 'success',
              startedAt: completedAt,
              completedAt,
              output,
            },
          ]
        })
        break
      }
    }
  }
}
