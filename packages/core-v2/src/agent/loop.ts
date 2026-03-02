/**
 * Simplified agent loop.
 *
 * Stream LLM → collect tool calls → run middleware → execute.
 * No doom loop, no validation gate, no hooks, no recovery inline.
 * Emits events; extensions subscribe and intercept via middleware.
 */

import { emitEvent, getAgentModes, getContextStrategies } from '../extensions/api.js'
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
import { executeTool, getToolDefinitions } from '../tools/registry.js'
import type { ToolContext } from '../tools/types.js'
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

function isRetryableError(message: string): boolean {
  return /rate.limit|overloaded|429|529|server error|too many requests/i.test(message)
}

function contentLength(content: MessageContent): number {
  if (typeof content === 'string') return content.length
  return content.reduce((sum, b) => {
    if (b.type === 'text') return sum + b.text.length
    if (b.type === 'tool_result') return sum + b.content.length
    if (b.type === 'image') return sum + 1000 // rough estimate for images
    return sum + b.name.length + JSON.stringify(b.input).length
  }, 0)
}

/** Estimate tokens from character count. ~1.3 tokens per word, ~5 chars per word. */
function estimateTokens(charCount: number): number {
  return Math.ceil((charCount / 5) * 1.3)
}

/** Known context window sizes by model prefix. */
const MODEL_CONTEXT_LIMITS: Record<string, number> = {
  'claude-opus': 200_000,
  'claude-sonnet': 200_000,
  'claude-haiku': 200_000,
  'gpt-4o': 128_000,
  'gpt-4': 128_000,
  'gpt-3.5': 16_000,
  gemini: 1_000_000,
  deepseek: 64_000,
}

function getContextLimit(model: string): number {
  for (const [prefix, limit] of Object.entries(MODEL_CONTEXT_LIMITS)) {
    if (model.startsWith(prefix)) return limit
  }
  return 200_000 // conservative default
}

/**
 * Select the best compaction strategy based on session length.
 * Uses "summarize" for longer sessions (>20 messages) to preserve context,
 * and "truncate" for shorter ones where simple trimming is sufficient.
 */
function selectCompactionStrategy(
  history: ChatMessage[]
): { name: string; compact: (messages: ChatMessage[], target: number) => ChatMessage[] } | null {
  const strategies = getContextStrategies()
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

/** Truncate tool result blocks that exceed byte limits. Mutates in-place. */
export async function truncateToolResults(blocks: ToolResultBlock[]): Promise<void> {
  let totalBytes = 0
  for (const block of blocks) {
    const bytes = Buffer.byteLength(block.content, 'utf8')
    if (bytes > MAX_RESULT_BYTES) {
      const savedPath = await saveOverflowOutput(block.content)
      const truncated = block.content.slice(0, MAX_RESULT_BYTES)
      const hint = savedPath ? `\n\n[Full output saved to: ${savedPath}]` : ''
      block.content = `${truncated}\n\n[...truncated ${bytes - MAX_RESULT_BYTES} bytes]${hint}`
    }
    totalBytes += Buffer.byteLength(block.content, 'utf8')
  }

  // If total exceeds limit, proportionally truncate the largest results
  if (totalBytes > MAX_TOTAL_RESULT_BYTES) {
    const ratio = MAX_TOTAL_RESULT_BYTES / totalBytes
    for (const block of blocks) {
      const bytes = Buffer.byteLength(block.content, 'utf8')
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
  private steeringMessage: string | null = null
  private steeringController: AbortController | null = null
  private stepCount = 0
  private stepLimitReached = false

  constructor(config: Partial<AgentConfig>, onEvent?: AgentEventCallback) {
    this.config = {
      ...DEFAULT_AGENT_CONFIG,
      maxTimeMinutes: config.maxTimeMinutes ?? 30,
      maxTurns: config.maxTurns ?? 50,
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
    this.steeringMessage = message
    this.steeringController?.abort()
  }

  async run(inputs: AgentInputs, signal: AbortSignal): Promise<AgentResult> {
    const startTime = Date.now()
    this.emit({ type: 'agent:start', agentId: this.agentId, goal: inputs.goal })

    const provider = this.config.provider ?? 'anthropic'
    const model = this.config.model ?? 'claude-sonnet-4-20250514'
    const client = createClient(provider)

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
      const systemPrompt = this.buildSystemPrompt(inputs)
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

        // Check for steering message injected between turns
        if (this.steeringMessage) {
          const msg = this.steeringMessage
          this.steeringMessage = null
          history.push({ role: 'user', content: msg })
          this.emit({ type: 'agent:steered', agentId: this.agentId, message: msg })
        }

        // Context window management — compact when usage exceeds threshold
        const contextLimit = getContextLimit(model)
        const threshold = this.config.compactionThreshold ?? 0.8
        const charCount = history.reduce((sum, m) => sum + contentLength(m.content), 0)
        const estimatedTokens = estimateTokens(charCount)
        if (estimatedTokens > contextLimit * threshold) {
          const messagesBefore = history.length
          const strategy = selectCompactionStrategy(history)
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
        this.emit({ type: 'turn:start', agentId: this.agentId, turn })

        // If step limit was reached last turn, pass empty tools to force text-only response
        const tools = this.stepLimitReached ? [] : this.getAvailableTools()

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
            turnSignal
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
            inputTokens: turnResult.usage.inputTokens,
            outputTokens: turnResult.usage.outputTokens,
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
        })
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
    signal: AbortSignal
  ): Promise<AgentTurnResult> {
    // Stream LLM response
    let assistantContent = ''
    const toolCalls: ToolUseBlock[] = []
    let turnInput = 0
    let turnOutput = 0

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
    }

    // Structured output — add __structured_output tool and force tool_choice
    const responseFormat = this.getEffectiveResponseFormat(lastUserMsg)
    if (responseFormat) {
      const structuredDef = buildStructuredOutputToolDefinition(responseFormat.schema)
      effectiveTools = [...tools, structuredDef]
      providerConfig.tools = effectiveTools
      providerConfig.toolChoice = { type: 'tool', name: STRUCTURED_OUTPUT_TOOL_NAME }
    }

    // Normalize messages for cross-provider compatibility
    const normalizedHistory = normalizeMessages(history)

    const stream = client.stream(
      [{ role: 'system', content: effectiveSystemPrompt }, ...normalizedHistory],
      providerConfig,
      signal
    )

    for await (const delta of stream) {
      if (signal.aborted) break

      if (delta.content) {
        assistantContent += delta.content
      }
      if (delta.toolUse) {
        toolCalls.push(delta.toolUse)
      }
      if (delta.usage) {
        turnInput += delta.usage.inputTokens ?? 0
        turnOutput += delta.usage.outputTokens ?? 0
      }
      if (delta.error) {
        this.emit({ type: 'error', agentId: this.agentId, error: delta.error.message })
        return {
          status: 'stop',
          terminateMode: AgentTerminateMode.ERROR,
          result: delta.error.message,
          usage: { inputTokens: turnInput, outputTokens: turnOutput },
        }
      }
    }

    // Add assistant message to history (structured content blocks)
    if (assistantContent || toolCalls.length > 0) {
      const blocks: ContentBlock[] = []
      if (assistantContent) blocks.push({ type: 'text', text: assistantContent })
      for (const call of toolCalls) blocks.push(call)
      history.push({ role: 'assistant', content: blocks })
      if (assistantContent) {
        this.emit({ type: 'thought', agentId: this.agentId, content: assistantContent })
      }
    }

    // No tool calls — assistant is done
    if (toolCalls.length === 0) {
      return {
        status: 'stop',
        terminateMode: AgentTerminateMode.GOAL,
        result: assistantContent,
        usage: { inputTokens: turnInput, outputTokens: turnOutput },
      }
    }

    // Check for completion tool before executing anything
    const completionCall = toolCalls.find((c) => c.name === COMPLETE_TASK_TOOL)
    if (completionCall) {
      const result = (completionCall.input as Record<string, string>).result ?? assistantContent
      emitEvent('agent:completing', { agentId: this.agentId, result })
      return {
        status: 'stop',
        terminateMode: AgentTerminateMode.GOAL,
        result,
        usage: { inputTokens: turnInput, outputTokens: turnOutput },
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
          usage: { inputTokens: turnInput, outputTokens: turnOutput },
        }
      }

      const jsonResult = JSON.stringify(structuredCall.input)
      emitEvent('agent:completing', { agentId: this.agentId, result: jsonResult })
      return {
        status: 'stop',
        terminateMode: AgentTerminateMode.GOAL,
        result: jsonResult,
        usage: { inputTokens: turnInput, outputTokens: turnOutput },
      }
    }

    // Execute tool calls — parallel or sequential based on config
    const results =
      (this.config.parallelToolExecution ?? true)
        ? await this.executeToolCallsParallel(toolCalls, cwd, model, signal)
        : await this.executeToolCallsSequential(toolCalls, cwd, model, signal)

    const callInfos: ToolCallInfo[] = []
    const toolResultBlocks: ToolResultBlock[] = []
    for (const { callInfo, resultBlock } of results) {
      callInfos.push(callInfo)
      toolResultBlocks.push(resultBlock)
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
        const suggestion = `Tool "${call.name}" has been called ${count} times with the same arguments. Try a different approach or tool.`
        history.push({ role: 'user', content: suggestion })
        this.emit({ type: 'doom-loop', agentId: this.agentId, tool: call.name, count })
      }
    }

    return {
      status: 'continue',
      result: assistantContent || undefined,
      toolCalls: callInfos,
      usage: { inputTokens: turnInput, outputTokens: turnOutput },
    }
  }

  /** Execute a single tool call, emitting events and building result objects. */
  private async executeOneToolCall(
    call: ToolUseBlock,
    cwd: string,
    model: string,
    signal: AbortSignal
  ): Promise<{ callInfo: ToolCallInfo; resultBlock: ToolResultBlock }> {
    if (signal.aborted) {
      return {
        callInfo: {
          name: call.name,
          args: call.input,
          result: 'Tool call skipped — user interrupted',
          success: false,
          durationMs: 0,
        },
        resultBlock: {
          type: 'tool_result' as const,
          tool_use_id: call.id,
          content: 'Tool call skipped — user interrupted',
          is_error: true,
        },
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
          callInfo: {
            name: call.name,
            args: call.input,
            result: errorMsg,
            success: false,
            durationMs: 0,
          },
          resultBlock: {
            type: 'tool_result' as const,
            tool_use_id: call.id,
            content: errorMsg,
            is_error: true,
          },
        }
      }
    }

    this.emit({
      type: 'tool:start',
      agentId: this.agentId,
      toolName: resolvedName,
      args: call.input,
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
    })

    return {
      callInfo: {
        name: resolvedName,
        args: call.input,
        result: output,
        success: result.success,
        durationMs,
      },
      resultBlock: {
        type: 'tool_result' as const,
        tool_use_id: call.id,
        content: efficientOutput,
        is_error: !result.success,
      },
    }
  }

  /** Execute all tool calls in parallel via Promise.all(). */
  private async executeToolCallsParallel(
    toolCalls: ToolUseBlock[],
    cwd: string,
    model: string,
    signal: AbortSignal
  ): Promise<{ callInfo: ToolCallInfo; resultBlock: ToolResultBlock }[]> {
    return Promise.all(toolCalls.map((call) => this.executeOneToolCall(call, cwd, model, signal)))
  }

  /** Execute tool calls sequentially, skipping remaining on abort. */
  private async executeToolCallsSequential(
    toolCalls: ToolUseBlock[],
    cwd: string,
    model: string,
    signal: AbortSignal
  ): Promise<{ callInfo: ToolCallInfo; resultBlock: ToolResultBlock }[]> {
    const results: { callInfo: ToolCallInfo; resultBlock: ToolResultBlock }[] = []
    for (const call of toolCalls) {
      results.push(await this.executeOneToolCall(call, cwd, model, signal))
    }
    return results
  }

  private buildSystemPrompt(inputs: AgentInputs): string {
    let prompt = this.config.systemPrompt ?? 'You are AVA, an AI coding assistant.'

    if (inputs.context) {
      prompt += `\n\n${inputs.context}`
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

  private getAvailableTools(): ToolDefinition[] {
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
    const result: AgentResult = {
      success: terminateMode === AgentTerminateMode.GOAL,
      terminateMode,
      output,
      turns,
      tokensUsed: { input: inputTokens, output: outputTokens },
      durationMs: Date.now() - startTime,
    }

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
