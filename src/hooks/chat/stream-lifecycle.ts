/**
 * Stream Lifecycle
 * Chat streaming via AgentExecutor — unified tool execution with middleware.
 */

import { type AgentEvent, AgentExecutor } from '@ava/core-v2/agent'
import {
  addToolMiddleware,
  type ToolMiddleware,
  type ToolMiddlewareContext,
} from '@ava/core-v2/extensions'
import { readFileContent } from '../../services/file-browser'
import { recordFileChange } from '../../services/file-versions'
import { resolveProvider } from '../../services/llm/bridge'
import { logDebug, logError, logInfo } from '../../services/logger'
import type { FileOperationType, ToolCall } from '../../types'
import { checkLintErrors, getModifiedFilePath } from './tool-execution'
import { buildToolCtx, type ChatDeps, type StreamOptions } from './types'

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

// ============================================================================
// Stream Response via AgentExecutor
// ============================================================================

export async function streamResponse(options: StreamOptions, deps: ChatDeps): Promise<void> {
  const provider = resolveProvider(options.model)
  deps.setCurrentProvider(provider)
  logDebug(deps.LOG_SRC, 'Stream start', {
    sessionId: options.sessionId,
    model: options.model,
    provider,
  })

  const allToolCalls: ToolCall[] = []
  let fullContent = ''
  let fullThinking = ''

  // Buffered tool call updates — prevents UI flickering on every progress chunk
  let toolFlushTimer: ReturnType<typeof setTimeout> | null = null
  let toolUpdatePending = false
  const flushToolUpdates = () => {
    if (!toolUpdatePending) return
    toolUpdatePending = false
    options.onToolUpdate?.([...allToolCalls])
  }
  const scheduleToolFlush = () => {
    toolUpdatePending = true
    if (toolFlushTimer !== null) return
    toolFlushTimer = setTimeout(() => {
      toolFlushTimer = null
      flushToolUpdates()
    }, 150)
  }
  const immediateToolFlush = () => {
    if (toolFlushTimer !== null) {
      clearTimeout(toolFlushTimer)
      toolFlushTimer = null
    }
    toolUpdatePending = true
    flushToolUpdates()
  }

  // Register temporary diff-capture middleware for file operations
  const diffMiddleware = createDiffCaptureMiddleware(deps, options.sessionId)
  const diffDisposable = addToolMiddleware(diffMiddleware)

  // Create AgentExecutor with chat-appropriate settings
  const executor = new AgentExecutor(
    {
      provider,
      model: options.model,
      maxTurns: 25,
      maxTimeMinutes: 10,
      parallelToolExecution: false,
      systemPrompt: options.systemPrompt,
      allowedTools: options.enableTools === false ? [] : undefined,
    },
    (event: AgentEvent) => {
      handleChatEvent(
        event,
        options,
        deps,
        allToolCalls,
        {
          get content() {
            return fullContent
          },
          set content(v: string) {
            fullContent = v
          },
          get thinking() {
            return fullThinking
          },
          set thinking(v: string) {
            fullThinking = v
          },
        },
        { scheduleToolFlush, immediateToolFlush }
      )
    }
  )

  try {
    const cwd = deps.currentProject()?.directory || '.'
    const result = await executor.run(
      {
        goal: options.goal,
        cwd,
        context: options.conversationContext,
      },
      options.signal
    )

    if (result) {
      const output = fullContent || result.output
      const totalTokens = result.tokensUsed.input + result.tokensUsed.output
      if (!output && allToolCalls.length === 0) {
        logInfo(deps.LOG_SRC, 'Stream completed with empty output', {
          terminateMode: result.terminateMode,
          turns: result.turns,
          success: result.success,
          tokensUsed: result.tokensUsed,
          error: result.error,
        })
      }
      options.onComplete(
        output || (result.error ? `Error: ${result.error}` : ''),
        totalTokens > 0 ? totalTokens : undefined,
        allToolCalls.length > 0 ? allToolCalls : undefined
      )
    } else {
      logInfo(deps.LOG_SRC, 'Stream returned null result')
      options.onComplete('(No response received)')
    }
  } catch (err) {
    if (err instanceof Error && err.name === 'AbortError') {
      logInfo(deps.LOG_SRC, 'Stream aborted', { sessionId: options.sessionId })
      // Still complete with whatever content we have so the bubble isn't blank
      if (fullContent || allToolCalls.length > 0) {
        options.onComplete(
          fullContent || '',
          undefined,
          allToolCalls.length > 0 ? allToolCalls : undefined
        )
      }
      return
    }
    const errorMsg = err instanceof Error ? err.message : 'Unknown error'
    logError(deps.LOG_SRC, 'Stream failed', err)
    options.onError({ type: 'unknown', message: errorMsg })
  } finally {
    if (toolFlushTimer !== null) {
      clearTimeout(toolFlushTimer)
      toolFlushTimer = null
    }
    diffDisposable.dispose()
  }
}

// ============================================================================
// Event Handler — maps AgentEvents to chat UI updates
// ============================================================================

interface ContentState {
  content: string
  thinking: string
}

interface ToolFlushControls {
  scheduleToolFlush: () => void
  immediateToolFlush: () => void
}

function handleChatEvent(
  event: AgentEvent,
  options: StreamOptions,
  deps: ChatDeps,
  allToolCalls: ToolCall[],
  state: ContentState,
  toolFlush?: ToolFlushControls
): void {
  switch (event.type) {
    case 'thought': {
      state.content += event.content
      options.onContent(state.content)
      break
    }

    case 'thinking': {
      state.thinking += event.content
      options.onThinking?.(state.thinking)
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
      }
      allToolCalls.push(tc)
      // Flush immediately on start so the UI shows the tool card right away
      if (toolFlush) toolFlush.immediateToolFlush()
      else options.onToolUpdate?.([...allToolCalls])
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
        // Capture tool output for expandable detail view
        if (event.output) {
          tc.output = event.output
        }
      }
      // Flush immediately on finish so status updates are instant
      if (toolFlush) toolFlush.immediateToolFlush()
      else options.onToolUpdate?.([...allToolCalls])
      break
    }

    case 'tool:progress': {
      const tc = [...allToolCalls]
        .reverse()
        .find((t: ToolCall) => t.name === event.toolName && t.status === 'running')
      if (tc) {
        tc.streamingOutput = (tc.streamingOutput ?? '') + event.chunk
        // Buffer progress updates to prevent flickering
        if (toolFlush) toolFlush.scheduleToolFlush()
        else options.onToolUpdate?.([...allToolCalls])
      }
      break
    }

    case 'error': {
      options.onError({ type: 'unknown', message: event.error })
      logError(deps.LOG_SRC, 'Agent error', { error: event.error })
      break
    }

    case 'doom-loop': {
      logInfo(deps.LOG_SRC, 'Doom loop detected', {
        tool: event.tool,
        count: event.count,
      })
      break
    }

    case 'context:compacting': {
      logInfo(deps.LOG_SRC, 'Context compacting', {
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

    default:
      break
  }
}

// ============================================================================
// Diff Capture Middleware
// ============================================================================

/**
 * Temporary middleware that captures file diffs during chat tool execution.
 * Registered per-stream and disposed after completion.
 */
function createDiffCaptureMiddleware(deps: ChatDeps, sessionId: string): ToolMiddleware {
  // Store original content keyed by tool_use_id equivalent
  const originalContents = new Map<string, string | null>()

  return {
    name: 'chat-diff-capture',
    priority: 25,

    async before(ctx: ToolMiddlewareContext) {
      const filePath = getModifiedFilePath(ctx.toolName, ctx.args)
      if (filePath && DIFF_TOOLS.has(ctx.toolName)) {
        try {
          const content = await readFileContent(filePath)
          originalContents.set(filePath, content && content.length <= MAX_CAPTURE ? content : null)
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
      deps.session.addFileOperation(fileOp)
      recordFileChange(sessionId, fileOp)

      // Lint check if enabled — return modified result via middleware contract
      if (deps.settings.settings().agentLimits.autoFixLint) {
        const toolCtx = buildToolCtx(deps, sessionId, new AbortController().signal)
        const lintErrors = await checkLintErrors(filePath, toolCtx)
        if (lintErrors) {
          return {
            result: {
              ...result,
              output: `${result.output}\n\nLint errors found:\n${lintErrors}\nPlease fix these issues.`,
            },
          }
        }
      }

      return undefined
    },
  }
}
