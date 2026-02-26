/**
 * Simplified agent loop.
 *
 * Stream LLM → collect tool calls → run middleware → execute.
 * No doom loop, no validation gate, no hooks, no recovery inline.
 * Emits events; extensions subscribe and intercept via middleware.
 */

import { emitEvent, getAgentModes } from '../extensions/api.js'
import { createClient } from '../llm/client.js'
import type {
  ChatMessage,
  LLMClient,
  ProviderConfig,
  ToolDefinition,
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

// ─── Agent Executor ──────────────────────────────────────────────────────────

export class AgentExecutor {
  readonly config: AgentConfig
  readonly agentId: string
  private onEvent?: AgentEventCallback

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

        turn++
        this.emit({ type: 'turn:start', agentId: this.agentId, turn })

        const tools = this.getAvailableTools()
        const turnResult = await this.executeTurn(
          client,
          systemPrompt,
          history,
          tools,
          inputs.cwd,
          model,
          combinedSignal
        )

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

    // Add assistant message to history
    if (assistantContent) {
      history.push({ role: 'assistant', content: assistantContent })
      this.emit({ type: 'thought', agentId: this.agentId, content: assistantContent })
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

    // Execute tool calls
    const callInfos: ToolCallInfo[] = []
    const toolResults: string[] = []

    for (const call of toolCalls) {
      // Check for completion tool
      if (call.name === COMPLETE_TASK_TOOL) {
        const result = (call.input as Record<string, string>).result ?? assistantContent
        emitEvent('agent:completing', { agentId: this.agentId, result })
        return {
          status: 'stop',
          terminateMode: AgentTerminateMode.GOAL,
          result,
          usage: { inputTokens: turnInput, outputTokens: turnOutput },
        }
      }

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
      }

      const result = await executeTool(call.name, call.input, ctx)
      const durationMs = Date.now() - toolStart

      callInfos.push({
        name: call.name,
        args: call.input,
        result: result.output,
        success: result.success,
        durationMs,
      })

      this.emit({
        type: 'tool:finish',
        agentId: this.agentId,
        toolName: call.name,
        success: result.success,
        durationMs,
      })

      toolResults.push(`<tool_result name="${call.name}">\n${result.output}\n</tool_result>`)
    }

    // Add tool results to history as user message
    history.push({ role: 'user', content: toolResults.join('\n\n') })

    return {
      status: 'continue',
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
