/**
 * Streaming Handler
 *
 * Manages the core execution flow: creates the AgentExecutor, handles all
 * streaming events (thought tokens, tool calls, thinking, context compaction,
 * agent finish, errors), and manages flush timers for batched UI updates.
 */

import type { AgentConfig, AgentEvent, AgentResult } from '@ava/core-v2/agent'
import { AgentExecutor } from '@ava/core-v2/agent'
import { addToolMiddleware } from '@ava/core-v2/extensions'
import type { ChatMessage } from '@ava/core-v2/llm'
import { batch } from 'solid-js'
import { estimateCost } from '../../lib/cost'
import { isOAuthSupported } from '../../services/auth/oauth'
import { getCoreBudget } from '../../services/core-bridge'
import { updateMessage } from '../../services/database'
import { resolveProvider } from '../../services/llm/bridge'
import { flushLogs, logInfo } from '../../services/logger'
import { notifyCompletion } from '../../services/notifications'
import type { Message, ToolCall } from '../../types'
import { buildAgentConfig, type ConfigDeps } from './config-builder'
import { createContentFlush, createThinkingFlush, createToolFlush } from './flush-timers'
import { createDiffCaptureMiddleware, getModifiedFilePath } from './tool-execution'
import type { AgentRefs, AgentSignals, SessionBridge } from './types'

// Re-export ConfigDeps so turn-manager can import from here
export type { ConfigDeps }

// ============================================================================
// Streaming Execution
// ============================================================================

export interface ExecuteDeps {
  signals: AgentSignals
  refs: AgentRefs
  session: SessionBridge
  handleAgentEvent: (event: AgentEvent) => void
  configDeps: ConfigDeps
}

/**
 * Core execution flow: creates an AgentExecutor, wires up streaming events,
 * and runs the agent. Returns the AgentResult or null if aborted.
 *
 * Shared by both `run()` and `_regenerate()` in the turn manager.
 */
export async function execute(
  goal: string,
  sessionId: string,
  assistantMsg: Message,
  priorMessages: ChatMessage[],
  model: string,
  deps: ExecuteDeps,
  configOverrides?: Partial<AgentConfig>
): Promise<AgentResult | null> {
  const { signals, refs, session, handleAgentEvent, configDeps } = deps
  const allToolCalls: ToolCall[] = []
  let accumulatedContent = ''

  // Reset streaming content signal
  signals.setStreamingContent('')

  // ── Flush timers ──────────────────────────────────────────────────────
  const contentFlush = createContentFlush(() => accumulatedContent, signals)
  const toolFlush = createToolFlush(() => allToolCalls, signals, session, assistantMsg.id)
  const thinkingFlush = createThinkingFlush(session)

  // Register temporary diff-capture middleware
  const diffMiddleware = createDiffCaptureMiddleware(sessionId, session)
  const diffDisposable = addToolMiddleware(diffMiddleware)

  try {
    const agentConfig = await buildAgentConfig(model, configDeps, configOverrides)

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
      permissionMode: configDeps.settingsRef.settings().permissionMode,
    })
    if (agentConfig.systemPrompt) {
      logInfo('Agent', 'System prompt preview', agentConfig.systemPrompt.slice(0, 500))
    }
    void flushLogs()

    // Create executor
    const executor = new AgentExecutor(agentConfig, (event: AgentEvent) => {
      handleAgentEvent(event)

      switch (event.type) {
        case 'thought': {
          accumulatedContent += event.content
          contentFlush.schedule()
          break
        }

        case 'thinking': {
          thinkingFlush.append(event.content)
          thinkingFlush.schedule(assistantMsg.id)
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
          toolFlush.immediate()
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
          toolFlush.immediate()
          break
        }

        case 'tool:progress': {
          const tc = [...allToolCalls]
            .reverse()
            .find((t: ToolCall) => t.name === event.toolName && t.status === 'running')
          if (tc) {
            tc.streamingOutput = (tc.streamingOutput ?? '') + event.chunk
            toolFlush.scheduleThrottled()
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
          contentFlush.flush()
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

          // Flush pending thinking
          thinkingFlush.finalize(assistantMsg.id)

          const inputTokens = event.result.tokensUsed.input
          const outputTokens = event.result.tokensUsed.output
          const provider = resolveProvider(model)
          const isFreeTier = isOAuthSupported(provider as Parameters<typeof isOAuthSupported>[0])
          const cost = isFreeTier
            ? undefined
            : (estimateCost(model, inputTokens, outputTokens) ?? undefined)
          const thinking = thinkingFlush.accumulated
          const meta: Record<string, unknown> = { costUSD: cost, model }
          if (allToolCalls.length > 0) meta.toolCalls = allToolCalls
          if (thinking) meta.thinking = thinking

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

          batch(() => {
            signals.setStreamingContent('')
            signals.setStreamingTokenEstimate(0)
            signals.setActiveToolCalls([])
          })
          getCoreBudget()?.addMessage(assistantMsg.id, finalContent)

          void notifyCompletion(
            event.result.success ? 'Agent complete' : 'Agent failed',
            (event.result.output ?? goal).slice(0, 100),
            configDeps.settingsRef.settings().notifications
          )
          break
        }

        case 'error': {
          batch(() => {
            signals.setError({ type: 'unknown', message: event.error })
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

    refs.executorRef.current = executor

    const cwd = configDeps.currentProjectDir() || '.'
    const result = await executor.run(
      {
        goal,
        cwd,
        messages: priorMessages.length > 0 ? priorMessages : undefined,
      },
      refs.abortRef.current!.signal
    )

    return result
  } finally {
    contentFlush.cleanup()
    toolFlush.cleanup()
    thinkingFlush.cleanup()
    diffDisposable.dispose()
    refs.executorRef.current = null
  }
}
