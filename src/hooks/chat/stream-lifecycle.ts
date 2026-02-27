/**
 * Stream Lifecycle
 * Core streaming loop: LLM → tool execution → continuation.
 */

import { executeTool, getToolDefinitions } from '@ava/core-v2/tools'
import { checkAutoApproval } from '../../lib/tool-approval'
import { createClient, getProviderForModel } from '../../services/llm/bridge'
import { logDebug, logError, logInfo } from '../../services/logger'
import type { ToolCall } from '../../types'
import type { ToolUseBlock } from '../../types/llm'
import { checkLintErrors, getModifiedFilePath } from './tool-execution'
import { buildToolCtx, type ChatDeps, type StreamOptions } from './types'

// ============================================================================
// Stream Response
// ============================================================================

export async function streamResponse(options: StreamOptions, deps: ChatDeps): Promise<void> {
  // Get provider for the model
  const provider = getProviderForModel(options.model)
  deps.setCurrentProvider(provider)
  logDebug(deps.LOG_SRC, 'Stream start', {
    sessionId: options.sessionId,
    model: options.model,
    provider,
  })

  // Create client via core-v2 (synchronous — providers registered by extensions)
  let client: ReturnType<typeof createClient>
  try {
    client = createClient(options.model)
  } catch (err) {
    options.onError({
      type: 'auth',
      message: err instanceof Error ? err.message : 'Failed to create client',
    })
    return
  }

  // Build messages array (may include tool results)
  const currentMessages = [...options.messages]
  let fullContent = ''

  // Tool execution context
  const toolCtx = buildToolCtx(deps, options.sessionId, options.signal)

  // Get tool definitions if tools are enabled
  const tools = options.enableTools !== false ? getToolDefinitions() : undefined
  logDebug(deps.LOG_SRC, 'Tool definitions', {
    count: tools?.length ?? 0,
    names: tools?.map((t) => t.name).slice(0, 5),
  })

  // Accumulate all tool calls across streaming iterations
  const allToolCalls: ToolCall[] = []

  // Stream loop - may iterate multiple times if tools are used
  let continueStreaming = true
  while (continueStreaming) {
    // Check abort before each streaming iteration
    if (options.signal.aborted) {
      logInfo(deps.LOG_SRC, 'Aborted before stream iteration', { sessionId: options.sessionId })
      return
    }
    continueStreaming = false
    const pendingToolUses: ToolUseBlock[] = []

    try {
      // Read generation settings at call time
      const gen = deps.settings.settings().generation
      for await (const delta of client.stream(
        currentMessages as Array<{
          role: 'user' | 'assistant' | 'system'
          content: string
        }>,
        {
          provider,
          model: options.model,
          authMethod: 'api-key', // Core handles auth method internally
          maxTokens: gen.maxTokens,
          temperature: gen.temperature,
          tools,
          thinking: gen.thinkingEnabled ? { enabled: true } : undefined,
        },
        options.signal
      )) {
        if (delta.error) {
          options.onError(delta.error)
          logError(deps.LOG_SRC, 'Stream error', delta.error)
          return
        }

        // Handle text content
        if (delta.content) {
          fullContent += delta.content
          options.onContent(fullContent)
        }

        // Handle tool use
        if (delta.toolUse) {
          pendingToolUses.push(delta.toolUse)

          // Create pending ToolCall and notify UI
          const input = delta.toolUse.input as Record<string, unknown>
          const tc: ToolCall = {
            id: delta.toolUse.id,
            name: delta.toolUse.name,
            args: input,
            status: 'pending',
            startedAt: Date.now(),
            filePath: getModifiedFilePath(delta.toolUse.name, input) ?? undefined,
          }
          allToolCalls.push(tc)
          options.onToolUpdate?.([...allToolCalls])
        }

        if (delta.done) {
          if (pendingToolUses.length > 0) {
            await executeToolLoop(
              pendingToolUses,
              allToolCalls,
              currentMessages,
              fullContent,
              options,
              deps,
              toolCtx
            )
            // Only continue if not aborted during tool execution
            continueStreaming = !options.signal.aborted
          } else {
            // No tools, we're done
            logDebug(deps.LOG_SRC, 'Stream done', {
              sessionId: options.sessionId,
              toolCalls: allToolCalls.length,
            })
            options.onComplete(
              fullContent,
              delta.usage?.totalTokens,
              allToolCalls.length > 0 ? allToolCalls : undefined
            )
          }
        }
      }
    } catch (err) {
      if (err instanceof Error && err.name === 'AbortError') {
        logInfo(deps.LOG_SRC, 'Stream aborted', { sessionId: options.sessionId })
        return // Silently handle abort
      }
      options.onError({
        type: 'unknown',
        message: err instanceof Error ? err.message : 'Unknown error',
      })
      logError(deps.LOG_SRC, 'Stream failed', err)
      return
    }
  }
}

// ============================================================================
// Tool Execution Loop (extracted from inner stream)
// ============================================================================

async function executeToolLoop(
  pendingToolUses: ToolUseBlock[],
  allToolCalls: ToolCall[],
  currentMessages: Array<{ role: 'user' | 'assistant' | 'system'; content: string | unknown[] }>,
  fullContent: string,
  options: StreamOptions,
  deps: ChatDeps,
  toolCtx: ReturnType<typeof buildToolCtx>
): Promise<void> {
  logDebug(deps.LOG_SRC, 'Tool execution start', {
    count: pendingToolUses.length,
    sessionId: options.sessionId,
  })

  // Add assistant message with tool uses to conversation
  const assistantContent = [
    ...(fullContent ? [{ type: 'text' as const, text: fullContent }] : []),
    ...pendingToolUses,
  ]
  currentMessages.push({ role: 'assistant', content: assistantContent })

  // Execute each tool and collect results
  const toolResults: Array<{
    type: 'tool_result'
    tool_use_id: string
    content: string
    is_error?: boolean
  }> = []

  for (const toolUse of pendingToolUses) {
    // Bail out if abort was signaled (stop button pressed)
    if (toolCtx.signal.aborted) {
      logInfo(deps.LOG_SRC, 'Tool loop aborted', { sessionId: options.sessionId })
      const tc = allToolCalls.find((t) => t.id === toolUse.id)
      if (tc) {
        tc.status = 'error'
        tc.error = 'Cancelled'
        tc.completedAt = Date.now()
      }
      options.onToolUpdate?.([...allToolCalls])
      break
    }

    const tc = allToolCalls.find((t) => t.id === toolUse.id)

    // Check auto-approval before executing
    const autoResult = checkAutoApproval(
      toolUse.name,
      toolUse.input as Record<string, unknown>,
      deps.settings.isToolAutoApproved
    )

    if (!autoResult.approved) {
      const approved = await deps.approval.requestApproval(
        toolUse.name,
        toolUse.input as Record<string, unknown>
      )
      if (!approved) {
        logInfo(deps.LOG_SRC, 'Tool denied', {
          toolName: toolUse.name,
          sessionId: options.sessionId,
        })
        if (tc) {
          tc.status = 'error'
          tc.error = 'User denied'
          tc.completedAt = Date.now()
        }
        options.onToolUpdate?.([...allToolCalls])
        toolResults.push({
          type: 'tool_result',
          tool_use_id: toolUse.id,
          content: 'User denied tool execution',
          is_error: true,
        })
        continue
      }
    }

    // Mark running
    if (tc) tc.status = 'running'
    options.onToolUpdate?.([...allToolCalls])

    const result = await executeTool(toolUse.name, toolUse.input, toolCtx)
    logDebug(deps.LOG_SRC, 'Tool finished', {
      toolName: toolUse.name,
      success: result.success,
    })
    let toolOutput = result.output

    // Lint check: if file was modified and autoFixLint is on, run linter
    if (result.success && deps.settings.settings().agentLimits.autoFixLint) {
      const filePath = getModifiedFilePath(toolUse.name, toolUse.input as Record<string, unknown>)
      if (filePath) {
        const lintErrors = await checkLintErrors(filePath, toolCtx)
        if (lintErrors) {
          toolOutput += `\n\nLint errors found:\n${lintErrors}\nPlease fix these issues.`
        }
      }
    }

    // Update ToolCall with result
    if (tc) {
      tc.status = result.success ? 'success' : 'error'
      tc.output = toolOutput.slice(0, 2000) // Cap stored output
      tc.error = result.error
      tc.completedAt = Date.now()
    }
    options.onToolUpdate?.([...allToolCalls])

    toolResults.push({
      type: 'tool_result',
      tool_use_id: toolUse.id,
      content: toolOutput,
      is_error: !result.success,
    })
  }

  // Add tool results as user message
  currentMessages.push({ role: 'user', content: toolResults })
}
