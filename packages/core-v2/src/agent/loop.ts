/**
 * Simplified agent loop.
 *
 * Stream LLM → collect tool calls → run middleware → execute.
 * No doom loop, no validation gate, no hooks, no recovery inline.
 * Emits events; extensions subscribe and intercept via middleware.
 */

import {
  callHook,
  emitEvent,
  emitEventAsync,
  getAgentModes,
  getContextStrategies,
} from '../extensions/api.js'
import { createClient } from '../llm/client.js'
import { normalizeMessages } from '../llm/normalize.js'
import type {
  ChatMessage,
  ContentBlock,
  LLMClient,
  MessageContent,
  ProviderConfig,
  ToolDefinition,
  ToolResultBlock,
  ToolUseBlock,
} from '../llm/types.js'
import { createLogger } from '../logger/logger.js'
import { executeTool, getToolDefinitions } from '../tools/registry.js'
import type { ToolContext } from '../tools/types.js'
import { getContextFallbackCandidate, getContextLimit } from './context-fallback.js'
import { efficientToolResult } from './efficient-results.js'
import { saveOverflowOutput } from './output-files.js'
import { repairToolName } from './repair.js'
import {
  buildStructuredOutputToolDefinition,
  STRUCTURED_OUTPUT_TOOL_NAME,
  validateStructuredOutput,
} from './structured-output.js'
import {
  type AgentConfig,
  type AgentEvent,
  type AgentEventCallback,
  type AgentInputs,
  type AgentResult,
  AgentTerminateMode,
  type AgentTurnResult,
  COMPLETE_TASK_TOOL,
  DEFAULT_AGENT_CONFIG,
  type ToolCallInfo,
} from './types.js'

// ─── Helpers ────────────────────────────────────────────────────────────────

const agentLog = createLogger('agent')
const loopLog = createLogger('agent:loop')
const llmLog = createLogger('llm')
const toolLog = createLogger('agent:tool')
const MAX_TOOL_CHAIN_DEPTH = 3

// Mirrors permissions SmartApprove read-only list for parallelization safety heuristic.
const READ_ONLY_TOOLS = new Set([
  'read_file',
  'glob',
  'grep',
  'ls',
  'websearch',
  'webfetch',
  'memory_read',
  'memory_list',
  'recall',
  'plan_enter',
  'lsp_hover',
  'lsp_definition',
  'lsp_references',
  'lsp_diagnostics',
  'lsp_workspace_symbols',
  'todoread',
  'question',
])

function isRetryableError(message: string): boolean {
  return /rate.limit|overloaded|429|529|server error|too many requests/i.test(message)
}

function contentLength(content: MessageContent): number {
  if (typeof content === 'string') return content.length
  return content.reduce((sum, b) => {
    if (b.type === 'text') return sum + b.text.length
    if (b.type === 'tool_result') return sum + (b.content?.length ?? 0)
    if (b.type === 'image') return sum + 1000 // rough estimate for images
    if (b.type === 'tool_use') return sum + b.name.length + JSON.stringify(b.input).length
    return sum
  }, 0)
}

/** Estimate tokens from character count. ~1.3 tokens per word, ~5 chars per word. */
function estimateTokens(charCount: number): number {
  return Math.ceil((charCount / 5) * 1.3)
}

/**
 * Select the best compaction strategy based on session length.
 * Uses "summarize" for longer sessions (>20 messages) to preserve context,
 * and "truncate" for shorter ones where simple trimming is sufficient.
 */
function selectCompactionStrategy(
  history: ChatMessage[],
  configured: string | string[] | undefined
): { name: string; compact: (messages: ChatMessage[], target: number) => ChatMessage[] } | null {
  const strategies = getContextStrategies()

  if (Array.isArray(configured) && configured.length > 0) {
    const selected = configured
      .map((name) => strategies.get(name))
      .filter(
        (
          strategy
        ): strategy is {
          name: string
          description: string
          compact: (messages: ChatMessage[], target: number) => ChatMessage[]
        } => strategy !== undefined
      )

    if (selected.length > 0) {
      return {
        name: `pipeline:${selected.map((s) => s.name).join(',')}`,
        compact(messages: ChatMessage[], target: number): ChatMessage[] {
          let current = messages
          for (const strategy of selected) {
            current = strategy.compact(current, target)
            const chars = current.reduce((sum, msg) => sum + contentLength(msg.content), 0)
            if (estimateTokens(chars) <= target) break
          }
          return current
        },
      }
    }
  }

  if (typeof configured === 'string') {
    const configuredStrategy = strategies.get(configured)
    if (configuredStrategy) return configuredStrategy
  }

  if (history.length > 20) {
    return strategies.get('summarize') ?? strategies.get('truncate') ?? null
  }
  return strategies.get('truncate') ?? strategies.get('summarize') ?? null
}

function sleep(ms: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve) => {
    const timer = setTimeout(resolve, ms)
    signal.addEventListener(
      'abort',
      () => {
        clearTimeout(timer)
        resolve()
      },
      { once: true }
    )
  })
}

// ─── Tool Result Truncation ──────────────────────────────────────────────────

const MAX_RESULT_BYTES = 50 * 1024 // 50KB per result
const MAX_TOTAL_RESULT_BYTES = 200 * 1024 // 200KB total

/** Platform-safe byte length — works in both browser (TextEncoder) and Node (Buffer). */
function byteLength(str: string): number {
  if (typeof Buffer !== 'undefined') return Buffer.byteLength(str, 'utf8')
  if (typeof TextEncoder !== 'undefined') return new TextEncoder().encode(str).byteLength
  // Rough fallback
  let bytes = 0
  for (let i = 0; i < str.length; i++) bytes += str.charCodeAt(i) > 0x7f ? 3 : 1
  return bytes
}

/** Truncate tool result blocks that exceed byte limits. Mutates in-place. */
export async function truncateToolResults(blocks: ToolResultBlock[]): Promise<void> {
  let totalBytes = 0
  for (const block of blocks) {
    const bytes = byteLength(block.content)
    if (bytes > MAX_RESULT_BYTES) {
      const savedPath = await saveOverflowOutput(block.content)
      const truncated = block.content.slice(0, MAX_RESULT_BYTES)
      const hint = savedPath ? `\n\n[Full output saved to: ${savedPath}]` : ''
      block.content = `${truncated}\n\n[...truncated ${bytes - MAX_RESULT_BYTES} bytes]${hint}`
    }
    totalBytes += byteLength(block.content)
  }

  // If total exceeds limit, proportionally truncate the largest results
  if (totalBytes > MAX_TOTAL_RESULT_BYTES) {
    const ratio = MAX_TOTAL_RESULT_BYTES / totalBytes
    for (const block of blocks) {
      const bytes = byteLength(block.content)
      if (bytes > 1024) {
        // only truncate results > 1KB
        const newLength = Math.floor(bytes * ratio)
        const truncated = block.content.slice(0, newLength)
        block.content = `${truncated}\n\n[...truncated ${bytes - newLength} bytes]`
      }
    }
  }
}

// ─── Per-Message Override Helpers ─────────────────────────────────────────────

/** Find the last user message in the history (for per-message overrides). */
function findLastUserMessage(history: ChatMessage[]): ChatMessage | undefined {
  for (let i = history.length - 1; i >= 0; i--) {
    if (history[i]?.role === 'user') return history[i]
  }
  return undefined
}

// ─── Agent Executor ──────────────────────────────────────────────────────────

export class AgentExecutor {
  readonly config: AgentConfig
  readonly agentId: string
  private onEvent?: AgentEventCallback
  private recentToolCalls: string[] = [] // hashes for doom loop detection
  private steeringQueue: string[] = []
  private followUpQueue: string[] = []
  private steeringController: AbortController | null = null
  private stepCount = 0
  private stepLimitReached = false
  private currentGoal = ''

  constructor(config: Partial<AgentConfig>, onEvent?: AgentEventCallback) {
    this.config = {
      ...DEFAULT_AGENT_CONFIG,
      maxTimeMinutes: config.maxTimeMinutes ?? 30,
      maxTurns: config.maxTurns ?? 50,
      steeringDeliveryMode: config.steeringDeliveryMode ?? 'one-at-a-time',
      ...config,
    }
    this.agentId = this.config.id ?? crypto.randomUUID()
    this.onEvent = onEvent
  }

  /**
   * Steer the agent with a new user message. Aborts the current tool execution
   * and injects the message into the conversation on the next loop iteration.
   */
  steer(message: string): void {
    this.steeringQueue.push(message)
    this.steeringController?.abort()
  }

  /** Queue a follow-up message to be injected after current turn completes. */
  queueFollowUp(message: string): void {
    this.followUpQueue.push(message)
    this.emit({ type: 'agent:follow-up-queued', agentId: this.agentId, message })
  }

  private popQueuedMessages(queue: string[]): string[] {
    if (queue.length === 0) return []
    if (this.config.steeringDeliveryMode === 'all') {
      const next = [...queue]
      queue.length = 0
      return next
    }
    const first = queue.shift()
    return first ? [first] : []
  }

  async run(inputs: AgentInputs, signal: AbortSignal): Promise<AgentResult> {
    const startTime = Date.now()
    this.currentGoal = inputs.goal
    this.emit({ type: 'agent:start', agentId: this.agentId, goal: inputs.goal })

    let provider = this.config.provider ?? 'anthropic'
    let model = this.config.model ?? 'claude-sonnet-4-20250514'
    agentLog.info('Run started', {
      goal_length: inputs.goal.length,
      session: this.agentId,
      model,
      provider,
    })
    let client = createClient(provider)

    const history: ChatMessage[] = []
    let totalInput = 0
    let totalOutput = 0
    let turn = 0
    let lastOutput = ''

    // Build timeout signal
    const timeoutMs = this.config.maxTimeMinutes * 60 * 1000
    const timeoutController = new AbortController()
    const timer = setTimeout(() => timeoutController.abort(), timeoutMs)
    const combinedSignal = AbortSignal.any([signal, timeoutController.signal])

    try {
      // Build initial messages
      const systemPrompt = await this.buildSystemPrompt(inputs)

      // Prepend structured conversation history if provided (desktop app multi-turn)
      if (inputs.messages?.length) {
        history.push(...inputs.messages)
      }

      history.push({ role: 'user', content: inputs.goal })

      const maxRetries = this.config.maxRetries ?? 3

      while (turn < this.config.maxTurns) {
        if (combinedSignal.aborted) {
          return this.finish(
            signal.aborted ? AgentTerminateMode.ABORTED : AgentTerminateMode.TIMEOUT,
            lastOutput,
            turn,
            totalInput,
            totalOutput,
            startTime
          )
        }

        // Check for steering messages injected between turns
        for (const msg of this.popQueuedMessages(this.steeringQueue)) {
          history.push({ role: 'user', content: msg })
          this.emit({ type: 'agent:steered', agentId: this.agentId, message: msg })
        }

        // Context window management — switch model first, then compact if needed
        const contextLimit = getContextLimit(model)
        const threshold = this.config.compactionThreshold ?? 0.8
        const charCount = history.reduce((sum, m) => sum + contentLength(m.content), 0)
        const estimatedTokens = estimateTokens(charCount)
        const contextFallbackThreshold = 0.9
        let switchedForContext = false

        if (estimatedTokens > contextLimit * contextFallbackThreshold) {
          const fallback = getContextFallbackCandidate(provider, model, estimatedTokens)
          if (fallback && (fallback.provider !== provider || fallback.model !== model)) {
            emitEvent('model:context-fallback', {
              from: { provider, model },
              to: { provider: fallback.provider, model: fallback.model },
              reason: 'context_overflow',
            })
            loopLog.info(
              `Switching from ${model} (${contextLimit} tokens) to ${fallback.model} (${fallback.contextWindow} tokens) due to context overflow`
            )
            provider = fallback.provider
            model = fallback.model
            client = createClient(provider)
            switchedForContext = true
          }
        }

        if (!switchedForContext && estimatedTokens > contextLimit * threshold) {
          const messagesBefore = history.length
          const strategy = selectCompactionStrategy(history, this.config.compactionStrategy)
          if (strategy) {
            const firstMsg = history[0]
            const compacted = strategy.compact(history, Math.floor(contextLimit * 0.5))
            // Ensure we keep first user message
            if (firstMsg && compacted[0] !== firstMsg) {
              compacted.unshift(firstMsg)
            }
            history.length = 0
            history.push(...compacted)
          }
          this.emit({
            type: 'context:compacting',
            agentId: this.agentId,
            estimatedTokens,
            contextLimit,
            messagesBefore,
            messagesAfter: history.length,
          })
          emitEvent('context:compacted', {
            agentId: this.agentId,
            tokensBefore: estimatedTokens,
            tokensAfter: estimateTokens(
              history.reduce((sum, m) => sum + contentLength(m.content), 0)
            ),
            messagesBefore,
            messagesAfter: history.length,
            strategy: strategy?.name ?? 'none',
          })
        }

        // Create a per-turn steering controller so steer() can abort the current turn
        this.steeringController = new AbortController()
        const turnSignal = AbortSignal.any([combinedSignal, this.steeringController.signal])

        turn++
        const turnStartedAt = Date.now()
        this.emit({ type: 'turn:start', agentId: this.agentId, turn })
        loopLog.info(`Turn ${turn} started`, { model, provider })

        // If step limit was reached last turn, pass empty tools to force text-only response
        const tools = this.stepLimitReached ? [] : await this.getAvailableTools()
        if (turn === 1) {
          console.debug(
            `[AVA:Agent] Turn 1 — ${tools.length} tools available, model=${model}, provider=${this.config.provider}`
          )
        }

        // If step limit was reached, inject a system message telling the agent to wrap up
        if (this.stepLimitReached) {
          history.push({
            role: 'user',
            content:
              'You have reached the maximum number of tool call steps. ' +
              'You must now provide a final text response summarizing what you accomplished. ' +
              'Do not attempt any more tool calls.',
          })
        }

        // Retry loop for transient LLM errors
        let turnResult: AgentTurnResult | undefined
        for (let attempt = 1; attempt <= maxRetries + 1; attempt++) {
          turnResult = await this.executeTurn(
            client,
            systemPrompt,
            history,
            tools,
            inputs.cwd,
            model,
            turnSignal,
            turn
          )

          // Check if this is a retryable error
          if (
            turnResult.status === 'stop' &&
            turnResult.terminateMode === AgentTerminateMode.ERROR &&
            turnResult.result &&
            isRetryableError(turnResult.result) &&
            attempt <= maxRetries
          ) {
            const delayMs = Math.min(1000 * 2 ** (attempt - 1), 30_000)
            this.emit({
              type: 'retry',
              agentId: this.agentId,
              attempt,
              maxRetries,
              delayMs,
              reason: turnResult.result,
            })
            await sleep(delayMs, combinedSignal)
            if (combinedSignal.aborted) break
            continue
          }

          break
        }

        // Clean up per-turn steering controller
        this.steeringController = null

        if (!turnResult) break

        // Accumulate token usage
        if (turnResult.usage) {
          totalInput += turnResult.usage.inputTokens
          totalOutput += turnResult.usage.outputTokens
          emitEvent('llm:usage', {
            sessionId: this.agentId,
            provider,
            model,
            inputTokens: turnResult.usage.inputTokens,
            outputTokens: turnResult.usage.outputTokens,
            cacheReadTokens: turnResult.usage.cacheReadTokens,
            cacheCreationTokens: turnResult.usage.cacheCreationTokens,
          })
        }

        if (turnResult.status === 'stop') {
          lastOutput = turnResult.result ?? lastOutput
          // If step limit forced this stop, use MAX_STEPS terminate mode
          const mode = this.stepLimitReached
            ? AgentTerminateMode.MAX_STEPS
            : turnResult.terminateMode
          this.emit({
            type: 'turn:end',
            agentId: this.agentId,
            turn,
            toolCalls: [],
            usage: turnResult.usage,
          })
          loopLog.info(`Turn ${turn} complete`, {
            tokens_in: turnResult.usage?.inputTokens ?? 0,
            tokens_out: turnResult.usage?.outputTokens ?? 0,
            duration_ms: Date.now() - turnStartedAt,
            tools_called: 0,
            completion: true,
          })
          return this.finish(mode, lastOutput, turn, totalInput, totalOutput, startTime)
        }

        // Capture assistant text even on tool-call turns (for MAX_TURNS output)
        if (turnResult.result) {
          lastOutput = turnResult.result
        }

        // Increment step counter by number of tool calls executed this turn
        if (turnResult.toolCalls) {
          this.stepCount += turnResult.toolCalls.length
        }

        // Check if step limit has been reached
        const maxSteps = this.config.maxSteps
        if (maxSteps !== undefined && this.stepCount >= maxSteps) {
          this.stepLimitReached = true
        }

        this.emit({
          type: 'turn:end',
          agentId: this.agentId,
          turn,
          toolCalls: turnResult.toolCalls,
          usage: turnResult.usage,
        })
        loopLog.info(`Turn ${turn} complete`, {
          tokens_in: turnResult.usage?.inputTokens ?? 0,
          tokens_out: turnResult.usage?.outputTokens ?? 0,
          duration_ms: Date.now() - turnStartedAt,
          tools_called: turnResult.toolCalls?.length ?? 0,
          completion: false,
        })

        // Inject follow-up queue after turn has fully completed
        for (const msg of this.popQueuedMessages(this.followUpQueue)) {
          history.push({ role: 'user', content: msg })
          this.emit({ type: 'agent:steered', agentId: this.agentId, message: msg })
        }
      }

      // Max turns reached
      return this.finish(
        AgentTerminateMode.MAX_TURNS,
        lastOutput,
        turn,
        totalInput,
        totalOutput,
        startTime
      )
    } finally {
      clearTimeout(timer)
    }
  }

  private async executeTurn(
    client: LLMClient,
    systemPrompt: string,
    history: ChatMessage[],
    tools: ToolDefinition[],
    cwd: string,
    model: string,
    signal: AbortSignal,
    turn?: number
  ): Promise<AgentTurnResult> {
    // Stream LLM response
    let assistantContent = ''
    const toolCalls: ToolUseBlock[] = []
    let turnInput = 0
    let turnOutput = 0
    let turnCacheRead = 0
    let turnCacheCreation = 0

    // Per-message overrides — check the last user message for metadata
    let effectiveSystemPrompt = systemPrompt
    const lastUserMsg = findLastUserMessage(history)
    if (lastUserMsg?._system) {
      effectiveSystemPrompt = `${lastUserMsg._system}\n\n${effectiveSystemPrompt}`
    }

    // Build provider config with structured output support
    let effectiveTools = tools
    const providerConfig: ProviderConfig = {
      provider: this.config.provider ?? 'anthropic',
      model,
      tools,
      toolChoice: tools.length > 0 ? this.resolveToolChoice(turn) : undefined,
      thinking: this.config.thinking,
    }

    // Structured output — add __structured_output tool and force tool_choice
    const responseFormat = this.getEffectiveResponseFormat(lastUserMsg)
    if (responseFormat) {
      const structuredDef = buildStructuredOutputToolDefinition(responseFormat.schema)
      effectiveTools = [...tools, structuredDef]
      providerConfig.tools = effectiveTools
      providerConfig.toolChoice = { type: 'tool', name: STRUCTURED_OUTPUT_TOOL_NAME }
    }

    // Let extensions process/transform history before normalization
    let historyForLLM = history
    try {
      const processed = await callHook<ChatMessage[], ChatMessage[]>(
        'history:process',
        history,
        history
      )
      historyForLLM = processed.output
    } catch (error) {
      console.warn('[AVA:Agent] history:process hook failed:', error)
    }

    // Normalize messages for cross-provider compatibility
    const normalizedHistory = normalizeMessages(historyForLLM)

    const stream = client.stream(
      [{ role: 'system', content: effectiveSystemPrompt }, ...normalizedHistory],
      providerConfig,
      signal
    )
    const llmStartedAt = Date.now()
    const estimatedTokens = estimateTokens(
      effectiveSystemPrompt.length +
        normalizedHistory.reduce((sum, item) => sum + contentLength(item.content), 0)
    )
    llmLog.info('Sending to LLM', {
      provider: providerConfig.provider,
      model,
      messages: normalizedHistory.length + 1,
      tokens_est: estimatedTokens,
    })

    for await (const delta of stream) {
      if (signal.aborted) break

      if (delta.content) {
        assistantContent += delta.content
        // Emit incremental content so the UI can stream tokens in real time
        this.emit({ type: 'thought', agentId: this.agentId, content: delta.content })
      }
      if (delta.thinking) {
        this.emit({ type: 'thinking', agentId: this.agentId, content: delta.thinking })
      }
      if (delta.toolUse) {
        toolCalls.push(delta.toolUse)
      }
      if (delta.usage) {
        turnInput += delta.usage.inputTokens ?? 0
        turnOutput += delta.usage.outputTokens ?? 0
        turnCacheRead += delta.usage.cacheReadTokens ?? 0
        turnCacheCreation += delta.usage.cacheCreationTokens ?? 0
      }
      if (delta.error) {
        llmLog.error('LLM call failed', {
          provider: providerConfig.provider,
          model,
          error: delta.error.message,
          retry: isRetryableError(delta.error.message),
        })
        this.emit({ type: 'error', agentId: this.agentId, error: delta.error.message })
        return {
          status: 'stop',
          terminateMode: AgentTerminateMode.ERROR,
          result: delta.error.message,
          usage: {
            inputTokens: turnInput,
            outputTokens: turnOutput,
            cacheReadTokens: turnCacheRead || undefined,
            cacheCreationTokens: turnCacheCreation || undefined,
          },
        }
      }
    }

    // Add assistant message to history (structured content blocks)
    if (assistantContent || toolCalls.length > 0) {
      const blocks: ContentBlock[] = []
      if (assistantContent) blocks.push({ type: 'text', text: assistantContent })
      for (const call of toolCalls) blocks.push(call)
      history.push({ role: 'assistant', content: blocks })
    }

    // No tool calls — assistant is done
    if (toolCalls.length === 0) {
      llmLog.info('LLM responded', {
        provider: providerConfig.provider,
        model,
        tokens_in: turnInput,
        tokens_out: turnOutput,
        duration_ms: Date.now() - llmStartedAt,
        tool_calls: 0,
      })
      return {
        status: 'stop',
        terminateMode: AgentTerminateMode.GOAL,
        result: assistantContent,
        usage: {
          inputTokens: turnInput,
          outputTokens: turnOutput,
          cacheReadTokens: turnCacheRead || undefined,
          cacheCreationTokens: turnCacheCreation || undefined,
        },
      }
    }

    // Check for completion tool before executing anything
    const completionCall = toolCalls.find((c) => c.name === COMPLETE_TASK_TOOL)
    if (completionCall) {
      llmLog.info('LLM responded', {
        provider: providerConfig.provider,
        model,
        tokens_in: turnInput,
        tokens_out: turnOutput,
        duration_ms: Date.now() - llmStartedAt,
        tool_calls: toolCalls.length,
      })
      const result = (completionCall.input as Record<string, string>).result ?? assistantContent
      emitEvent('agent:completing', { agentId: this.agentId, result, goal: this.currentGoal })
      return {
        status: 'stop',
        terminateMode: AgentTerminateMode.GOAL,
        result,
        usage: {
          inputTokens: turnInput,
          outputTokens: turnOutput,
          cacheReadTokens: turnCacheRead || undefined,
          cacheCreationTokens: turnCacheCreation || undefined,
        },
      }
    }

    // Check for structured output tool — extract and validate the result
    const structuredCall = toolCalls.find((c) => c.name === STRUCTURED_OUTPUT_TOOL_NAME)
    if (structuredCall && responseFormat) {
      const errors = validateStructuredOutput(structuredCall.input, responseFormat.schema)
      if (errors.length > 0) {
        // Validation failed — add error as tool result and continue
        history.push({
          role: 'user',
          content: [
            {
              type: 'tool_result' as const,
              tool_use_id: structuredCall.id,
              content: `Structured output validation failed:\n${errors.join('\n')}\n\nPlease fix and try again.`,
              is_error: true,
            },
          ],
        })
        return {
          status: 'continue',
          result: assistantContent || undefined,
          toolCalls: [
            {
              name: STRUCTURED_OUTPUT_TOOL_NAME,
              args: structuredCall.input,
              result: `Validation failed: ${errors.join(', ')}`,
              success: false,
              durationMs: 0,
            },
          ],
          usage: {
            inputTokens: turnInput,
            outputTokens: turnOutput,
            cacheReadTokens: turnCacheRead || undefined,
            cacheCreationTokens: turnCacheCreation || undefined,
          },
        }
      }

      const jsonResult = JSON.stringify(structuredCall.input)
      llmLog.info('LLM responded', {
        provider: providerConfig.provider,
        model,
        tokens_in: turnInput,
        tokens_out: turnOutput,
        duration_ms: Date.now() - llmStartedAt,
        tool_calls: toolCalls.length,
      })
      emitEvent('agent:completing', {
        agentId: this.agentId,
        result: jsonResult,
        goal: this.currentGoal,
      })
      return {
        status: 'stop',
        terminateMode: AgentTerminateMode.GOAL,
        result: jsonResult,
        usage: {
          inputTokens: turnInput,
          outputTokens: turnOutput,
          cacheReadTokens: turnCacheRead || undefined,
          cacheCreationTokens: turnCacheCreation || undefined,
        },
      }
    }

    // Execute tool calls — parallel or sequential based on config
    llmLog.info('LLM responded', {
      provider: providerConfig.provider,
      model,
      tokens_in: turnInput,
      tokens_out: turnOutput,
      duration_ms: Date.now() - llmStartedAt,
      tool_calls: toolCalls.length,
    })
    const results =
      (this.config.parallelToolExecution ?? true)
        ? await this.executeToolCallsParallel(toolCalls, cwd, model, signal)
        : await this.executeToolCallsSequential(toolCalls, cwd, model, signal)

    const callInfos: ToolCallInfo[] = []
    const toolResultBlocks: ToolResultBlock[] = []
    for (const result of results) {
      callInfos.push(...result.callInfos)
      toolResultBlocks.push(...result.resultBlocks)
    }

    // Ensure chained tail-call tool results have matching tool_use blocks in history.
    const assistantMsg = history[history.length - 1]
    if (assistantMsg?.role === 'assistant' && Array.isArray(assistantMsg.content)) {
      for (const result of results) {
        for (let i = 1; i < result.callInfos.length; i++) {
          const info = result.callInfos[i]
          const block = result.resultBlocks[i]
          if (!info || !block) continue
          assistantMsg.content.push({
            type: 'tool_use',
            id: block.tool_use_id,
            name: info.name,
            input: info.args,
          })
        }
      }
    }

    // Truncate tool results that exceed byte limits before adding to history
    await truncateToolResults(toolResultBlocks)

    // Add tool results to history as structured user message
    history.push({ role: 'user', content: toolResultBlocks })

    // Doom loop detection — check for consecutive identical tool calls
    for (const call of toolCalls) {
      const hash = `${call.name}:${JSON.stringify(call.input)}`
      this.recentToolCalls.push(hash)
      if (this.recentToolCalls.length > 10) this.recentToolCalls.shift()

      // Count consecutive identical calls from end
      let count = 0
      for (let i = this.recentToolCalls.length - 1; i >= 0; i--) {
        if (this.recentToolCalls[i] === hash) count++
        else break
      }

      if (count >= 3) {
        loopLog.warn('Possible loop detected', {
          tool: call.name,
          same_tool_count: count,
        })
        const suggestion = `Tool "${call.name}" has been called ${count} times with the same arguments. Try a different approach or tool.`
        history.push({ role: 'user', content: suggestion })
        this.emit({ type: 'doom-loop', agentId: this.agentId, tool: call.name, count })
      }
    }

    return {
      status: 'continue',
      result: assistantContent || undefined,
      toolCalls: callInfos,
      usage: {
        inputTokens: turnInput,
        outputTokens: turnOutput,
        cacheReadTokens: turnCacheRead || undefined,
        cacheCreationTokens: turnCacheCreation || undefined,
      },
    }
  }

  /** Execute a single tool call, emitting events and building result objects. */
  private async executeOneToolCall(
    call: ToolUseBlock,
    cwd: string,
    model: string,
    signal: AbortSignal,
    chainDepth = 0
  ): Promise<{ callInfos: ToolCallInfo[]; resultBlocks: ToolResultBlock[] }> {
    if (signal.aborted) {
      return {
        callInfos: [
          {
            name: call.name,
            args: call.input,
            result: 'Tool call skipped — user interrupted',
            success: false,
            durationMs: 0,
          },
        ],
        resultBlocks: [
          {
            type: 'tool_result' as const,
            tool_use_id: call.id,
            content: 'Tool call skipped — user interrupted',
            is_error: true,
          },
        ],
      }
    }

    // Repair tool name if it doesn't exist in available tools
    const availableNames = getToolDefinitions().map((t) => t.name)
    let resolvedName = call.name
    if (!availableNames.includes(call.name)) {
      const repaired = repairToolName(call.name, availableNames)
      if (repaired) {
        resolvedName = repaired
      } else {
        const available = availableNames.slice(0, 20).join(', ')
        const errorMsg = `Unknown tool "${call.name}". Available tools: ${available}${availableNames.length > 20 ? ` (and ${availableNames.length - 20} more)` : ''}`
        return {
          callInfos: [
            {
              name: call.name,
              args: call.input,
              result: errorMsg,
              success: false,
              durationMs: 0,
            },
          ],
          resultBlocks: [
            {
              type: 'tool_result' as const,
              tool_use_id: call.id,
              content: errorMsg,
              is_error: true,
            },
          ],
        }
      }
    }

    this.emit({
      type: 'tool:start',
      agentId: this.agentId,
      toolName: resolvedName,
      args: call.input,
    })
    toolLog.info('Tool called', {
      tool: resolvedName,
      arg_keys: Object.keys((call.input as Record<string, unknown>) ?? {}).join(','),
    })
    const toolStart = Date.now()

    const onProgress = (data: { chunk: string }) => {
      this.emit({
        type: 'tool:progress',
        agentId: this.agentId,
        toolName: resolvedName,
        chunk: data.chunk,
      })
    }

    const ctx: ToolContext = {
      sessionId: this.agentId,
      workingDirectory: cwd,
      signal,
      provider: this.config.provider,
      model,
      onEvent: this.onEvent as ToolContext['onEvent'],
      onProgress,
      delegationDepth: this.config.delegationDepth,
    }

    const result = await executeTool(resolvedName, call.input, ctx)
    const durationMs = Date.now() - toolStart
    const output = result.success ? result.output : result.error || result.output || 'Tool failed'

    // Apply token-efficient compression for the LLM-facing content
    const efficientOutput = result.success ? efficientToolResult(resolvedName, output) : output

    this.emit({
      type: 'tool:finish',
      agentId: this.agentId,
      toolName: resolvedName,
      success: result.success,
      durationMs,
      output,
    })
    toolLog.info('Tool finished', {
      tool: resolvedName,
      status: result.success ? 'ok' : 'error',
      duration_ms: durationMs,
    })

    const currentCallInfo: ToolCallInfo = {
      name: resolvedName,
      args: call.input,
      result: output,
      success: result.success,
      durationMs,
    }
    const currentResultBlock: ToolResultBlock = {
      type: 'tool_result' as const,
      tool_use_id: call.id,
      content: efficientOutput,
      is_error: !result.success,
    }

    if (result.nextToolCall) {
      if (chainDepth >= MAX_TOOL_CHAIN_DEPTH) {
        const warning =
          '\n\n[Tool chain truncated: maximum tail-call depth reached; skipping nextToolCall]'
        return {
          callInfos: [currentCallInfo],
          resultBlocks: [
            { ...currentResultBlock, content: `${currentResultBlock.content}${warning}` },
          ],
        }
      }

      const chainedCall: ToolUseBlock = {
        type: 'tool_use',
        id: `${call.id}:chain:${chainDepth + 1}`,
        name: result.nextToolCall.name,
        input: result.nextToolCall.input,
      }
      const chained = await this.executeOneToolCall(chainedCall, cwd, model, signal, chainDepth + 1)
      return {
        callInfos: [currentCallInfo, ...chained.callInfos],
        resultBlocks: [currentResultBlock, ...chained.resultBlocks],
      }
    }

    return {
      callInfos: [currentCallInfo],
      resultBlocks: [currentResultBlock],
    }
  }

  /** Execute tool calls in parallel only when all are read-only. */
  private async executeToolCallsParallel(
    toolCalls: ToolUseBlock[],
    cwd: string,
    model: string,
    signal: AbortSignal
  ): Promise<{ callInfos: ToolCallInfo[]; resultBlocks: ToolResultBlock[] }[]> {
    if (!this.areToolCallsIndependent(toolCalls)) {
      return this.executeToolCallsSequential(toolCalls, cwd, model, signal)
    }
    return Promise.all(toolCalls.map((call) => this.executeOneToolCall(call, cwd, model, signal)))
  }

  /** Execute tool calls sequentially, skipping remaining on abort. */
  private async executeToolCallsSequential(
    toolCalls: ToolUseBlock[],
    cwd: string,
    model: string,
    signal: AbortSignal
  ): Promise<{ callInfos: ToolCallInfo[]; resultBlocks: ToolResultBlock[] }[]> {
    const results: { callInfos: ToolCallInfo[]; resultBlocks: ToolResultBlock[] }[] = []
    for (let index = 0; index < toolCalls.length; index++) {
      if (this.steeringQueue.length > 0) {
        const skipped = this.skipPendingTools(toolCalls, index)
        if (skipped.length > 0) {
          const notice = `[Steering interrupt: ${skipped.length} pending tool calls skipped. User message follows.]`
          this.emit({
            type: 'agent:tools-skipped',
            agentId: this.agentId,
            skippedTools: skipped,
            reason: 'steering',
          })
          results.push({
            callInfos: [],
            resultBlocks: toolCalls.slice(index).map((call) => ({
              type: 'tool_result' as const,
              tool_use_id: call.id,
              content: notice,
              is_error: true,
            })),
          })
        }
        break
      }

      const call = toolCalls[index]
      if (!call) continue
      results.push(await this.executeOneToolCall(call, cwd, model, signal))
    }
    return results
  }

  private skipPendingTools(toolCalls: ToolUseBlock[], startIndex: number): string[] {
    return toolCalls.slice(startIndex).map((call) => call.name)
  }

  private areToolCallsIndependent(toolCalls: ToolUseBlock[]): boolean {
    if (toolCalls.length <= 1) return true
    return toolCalls.every((call) => READ_ONLY_TOOLS.has(call.name))
  }

  /**
   * Resolve tool_choice based on the configured strategy and current turn.
   * - 'auto': always { type: 'auto' } (default)
   * - 'required': always { type: 'required' }
   * - 'required-first': { type: 'required' } on turn 1, { type: 'auto' } after (legacy opt-in)
   */
  private resolveToolChoice(turn?: number): { type: 'auto' } | { type: 'required' } {
    const strategy = this.config.toolChoiceStrategy ?? 'auto'
    if (strategy === 'required') return { type: 'required' }
    if (strategy === 'required-first' && turn === 1) return { type: 'required' }
    return { type: 'auto' }
  }

  private async buildSystemPrompt(inputs: AgentInputs): Promise<string> {
    let prompt = this.config.systemPrompt ?? 'You are AVA, an AI coding assistant.'

    if (inputs.context) {
      prompt += `\n\n${inputs.context}`
    }

    const sections: string[] = []
    try {
      await emitEventAsync('prompt:build', { sections })
    } catch (error) {
      loopLog.warn('prompt:build event failed', {
        error: error instanceof Error ? error.message : String(error),
      })
    }
    if (sections.length > 0) {
      prompt += `\n\n${sections.join('\n\n')}`
    }

    prompt += `\n\nWorking directory: ${inputs.cwd}`

    // Let active agent mode modify the prompt
    if (this.config.toolMode) {
      const mode = getAgentModes().get(this.config.toolMode)
      if (mode?.systemPrompt) {
        prompt = mode.systemPrompt(prompt)
      }
    }

    return prompt
  }

  private async getAvailableTools(): Promise<ToolDefinition[]> {
    let tools = getToolDefinitions()

    // Filter by allowedTools if set (for subagents)
    if (this.config.allowedTools?.length) {
      const allowed = new Set(this.config.allowedTools)
      tools = tools.filter((t) => allowed.has(t.name))
    }

    // Let active agent mode filter tools
    if (this.config.toolMode) {
      const mode = getAgentModes().get(this.config.toolMode)
      if (mode?.filterTools) {
        tools = mode.filterTools(tools)
      }
    }

    try {
      const described = await callHook<ToolDefinition[], ToolDefinition[]>(
        'tool:describe',
        tools,
        tools
      )
      tools = described.output
    } catch (error) {
      loopLog.warn('tool:describe hook failed', {
        error: error instanceof Error ? error.message : String(error),
      })
    }

    return tools
  }

  private finish(
    terminateMode: AgentTerminateMode,
    output: string,
    turns: number,
    inputTokens: number,
    outputTokens: number,
    startTime: number
  ): AgentResult {
    const durationMs = Date.now() - startTime
    const result: AgentResult = {
      success: terminateMode === AgentTerminateMode.GOAL,
      terminateMode,
      output,
      turns,
      tokensUsed: { input: inputTokens, output: outputTokens },
      durationMs,
    }

    agentLog.info('Run complete', {
      turns,
      total_tokens: inputTokens + outputTokens,
      duration_ms: durationMs,
      status: result.success ? 'success' : terminateMode,
    })

    this.emit({ type: 'agent:finish', agentId: this.agentId, result })
    return result
  }

  /**
   * Determine the effective response format for this turn.
   * Checks per-message _format override first, then falls back to config.
   */
  private getEffectiveResponseFormat(
    lastUserMsg: ChatMessage | undefined
  ): { type: 'json_object'; schema: Record<string, unknown> } | undefined {
    // Per-message _format override: 'json' activates structured output if schema exists
    if (lastUserMsg?._format === 'json' && this.config.responseFormat) {
      return this.config.responseFormat
    }
    // Per-message _format of 'text' disables structured output for this turn
    if (lastUserMsg?._format === 'text') {
      return undefined
    }
    return this.config.responseFormat
  }

  private emit(event: AgentEvent): void {
    this.onEvent?.(event)
    emitEvent(event.type, event)
  }
}

// ─── Convenience ─────────────────────────────────────────────────────────────

export async function runAgent(
  inputs: AgentInputs,
  config: Partial<AgentConfig>,
  signal: AbortSignal,
  onEvent?: AgentEventCallback
): Promise<AgentResult> {
  const executor = new AgentExecutor(config, onEvent)
  return executor.run(inputs, signal)
}
