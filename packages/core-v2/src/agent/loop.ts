/**
 * Simplified agent loop.
 *
 * Stream LLM → collect tool calls → run middleware → execute.
 * No doom loop, no validation gate, no hooks, no recovery inline.
 * Emits events; extensions subscribe and intercept via middleware.
 */

import { emitEvent, getAgentModes, getContextStrategies } from '../extensions/api.js'
import { createClient } from '../llm/client.js'
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
    return sum + b.name.length + JSON.stringify(b.input).length
  }, 0)
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

// ─── Agent Executor ──────────────────────────────────────────────────────────

export class AgentExecutor {
  readonly config: AgentConfig
  readonly agentId: string
  private onEvent?: AgentEventCallback
  private recentToolCalls: string[] = [] // hashes for doom loop detection

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

        // Context window management — compact if >80% of limit
        const contextLimit = getContextLimit(model)
        const estimatedTokens = history.reduce((sum, m) => sum + contentLength(m.content), 0) / 4
        if (estimatedTokens > contextLimit * 0.8) {
          const strategy = getContextStrategies().get('truncate')
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
        }

        turn++
        this.emit({ type: 'turn:start', agentId: this.agentId, turn })

        const tools = this.getAvailableTools()

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
            combinedSignal
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
          this.emit({
            type: 'turn:end',
            agentId: this.agentId,
            turn,
            toolCalls: [],
          })
          return this.finish(
            turnResult.terminateMode,
            lastOutput,
            turn,
            totalInput,
            totalOutput,
            startTime
          )
        }

        // Capture assistant text even on tool-call turns (for MAX_TURNS output)
        if (turnResult.result) {
          lastOutput = turnResult.result
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

    const providerConfig: ProviderConfig = {
      provider: this.config.provider ?? 'anthropic',
      model,
      tools,
    }

    const stream = client.stream(
      [{ role: 'system', content: systemPrompt }, ...history],
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

    // Execute all tool calls in parallel (Promise.all preserves order)
    const results = await Promise.all(
      toolCalls.map(async (call) => {
        this.emit({
          type: 'tool:start',
          agentId: this.agentId,
          toolName: call.name,
          args: call.input,
        })
        const toolStart = Date.now()

        const ctx: ToolContext = {
          sessionId: this.agentId,
          workingDirectory: cwd,
          signal,
          provider: this.config.provider,
          model,
        }

        const result = await executeTool(call.name, call.input, ctx)
        const durationMs = Date.now() - toolStart
        const output = result.success
          ? result.output
          : result.error || result.output || 'Tool failed'

        this.emit({
          type: 'tool:finish',
          agentId: this.agentId,
          toolName: call.name,
          success: result.success,
          durationMs,
        })

        return {
          callInfo: {
            name: call.name,
            args: call.input,
            result: output,
            success: result.success,
            durationMs,
          } as ToolCallInfo,
          resultBlock: {
            type: 'tool_result' as const,
            tool_use_id: call.id,
            content: output,
            is_error: !result.success,
          } as ToolResultBlock,
        }
      })
    )

    const callInfos: ToolCallInfo[] = []
    const toolResultBlocks: ToolResultBlock[] = []
    for (const { callInfo, resultBlock } of results) {
      callInfos.push(callInfo)
      toolResultBlocks.push(resultBlock)
    }

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
