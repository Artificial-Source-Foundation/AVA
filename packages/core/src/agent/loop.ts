/**
 * Agent Loop
 * Main agent executor implementing the autonomous loop
 *
 * Based on Gemini CLI's local-executor.ts pattern
 */

import {
  createTaskCancelContext,
  createTaskCompleteContext,
  createTaskStartContext,
  getHookRunner,
} from '../hooks/index.js'
import { createClient, getAuth } from '../llm/client.js'
import {
  executeTool,
  getToolCallCount,
  getToolDefinitions,
  resetToolCallCount,
} from '../tools/registry.js'
import type { ToolContext } from '../tools/types.js'
import type { ChatMessage, ToolDefinition } from '../types/llm.js'
import {
  type AgentConfig,
  type AgentEvent,
  type AgentEventCallback,
  type AgentInputs,
  type AgentResult,
  type AgentStep,
  AgentTerminateMode,
  type AgentTurnResult,
  COMPLETE_TASK_TOOL,
  DEFAULT_AGENT_CONFIG,
  type ToolCallInfo,
} from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Number of identical consecutive calls to trigger doom loop detection */
const DOOM_LOOP_THRESHOLD = 3

/** Generate random agent ID */
function generateAgentId(): string {
  return `agent-${Math.random().toString(36).slice(2, 8)}`
}

/** Generate random session ID */
function generateSessionId(): string {
  return `session-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
}

// ============================================================================
// Agent Executor
// ============================================================================

/**
 * Executes an autonomous agent loop
 *
 * The agent will:
 * 1. Plan and execute steps using available tools
 * 2. Self-correct on errors
 * 3. Call complete_task when done
 * 4. Terminate on timeout, max turns, or abort
 */
/** Record of a tool call for doom loop detection */
interface ToolCallRecord {
  name: string
  argsHash: string
  turn: number
}

/**
 * Hash tool arguments for comparison
 */
function hashArgs(args: Record<string, unknown>): string {
  return JSON.stringify(args, Object.keys(args).sort())
}

/**
 * Detect if agent is in a doom loop (repeated identical tool calls)
 */
function detectDoomLoop(history: ToolCallRecord[]): boolean {
  if (history.length < DOOM_LOOP_THRESHOLD) {
    return false
  }

  // Get last N calls
  const recent = history.slice(-DOOM_LOOP_THRESHOLD)

  // Check if all are identical (same tool name and args)
  const first = recent[0]
  return recent.every((call) => call.name === first.name && call.argsHash === first.argsHash)
}

export class AgentExecutor {
  readonly config: AgentConfig
  readonly agentId: string

  private onEvent?: AgentEventCallback
  private steps: AgentStep[] = []
  private turnCounter = 0
  private tokensUsed = 0
  private sessionId: string
  private taskStartTime = 0
  private totalToolCalls = 0
  private toolCallHistory: ToolCallRecord[] = []
  private doomLoopDetected = false

  constructor(config: Partial<AgentConfig>, onEvent?: AgentEventCallback) {
    this.config = {
      ...DEFAULT_AGENT_CONFIG,
      maxTimeMinutes: config.maxTimeMinutes ?? 10,
      maxTurns: config.maxTurns ?? 20,
      ...config,
    }
    this.agentId = config.id ?? generateAgentId()
    this.sessionId = generateSessionId()
    this.onEvent = onEvent
  }

  /**
   * Run the agent loop
   *
   * @param inputs - Agent inputs including goal
   * @param signal - AbortSignal for cancellation
   * @returns Agent result
   */
  async run(inputs: AgentInputs, signal: AbortSignal): Promise<AgentResult> {
    const startTime = Date.now()
    let terminateMode: AgentTerminateMode = AgentTerminateMode.ERROR
    let finalResult: string | null = null

    // Set up timeout
    const timeoutController = new AbortController()
    const timeoutId = setTimeout(
      () => timeoutController.abort(new Error('Agent timed out.')),
      this.config.maxTimeMinutes * 60 * 1000
    )

    // Combine signals
    const combinedSignal = AbortSignal.any([signal, timeoutController.signal])

    // Emit start event
    this.emit({
      type: 'agent:start',
      agentId: this.agentId,
      timestamp: Date.now(),
      goal: inputs.goal,
      config: this.config,
    })

    // Track task start time
    this.taskStartTime = startTime

    // Run TaskStart hook
    try {
      const hookRunner = getHookRunner(inputs.cwd)
      await hookRunner.run(
        'TaskStart',
        createTaskStartContext({
          goal: inputs.goal,
          sessionId: this.sessionId,
          workingDirectory: inputs.cwd,
        })
      )
    } catch {
      // Don't fail on hook errors
    }

    try {
      // Resolve auth
      const provider = this.config.provider ?? 'anthropic'
      const auth = await getAuth(provider)
      if (!auth) {
        throw new Error(`No authentication configured for provider: ${provider}`)
      }

      // Create LLM client
      const client = await createClient(provider)

      // Build conversation history
      const history: ChatMessage[] = []

      // Add system message
      const systemPrompt = this.buildSystemPrompt(inputs)
      history.push({ role: 'system', content: systemPrompt })

      // Add initial user message with the goal
      history.push({ role: 'user', content: inputs.goal })

      // Get available tools
      const tools = this.getAvailableTools()

      // Main agent loop
      while (true) {
        // Check termination conditions
        const checkResult = this.checkTermination(startTime)
        if (checkResult) {
          terminateMode = checkResult
          break
        }

        // Check for abort
        if (combinedSignal.aborted) {
          terminateMode = timeoutController.signal.aborted
            ? AgentTerminateMode.TIMEOUT
            : AgentTerminateMode.ABORTED

          // Run TaskCancel hook
          try {
            const hookRunner = getHookRunner(inputs.cwd)
            await hookRunner.run(
              'TaskCancel',
              createTaskCancelContext({
                reason: terminateMode === AgentTerminateMode.TIMEOUT ? 'Timeout' : 'Aborted',
                sessionId: this.sessionId,
                workingDirectory: inputs.cwd,
                durationMs: Date.now() - this.taskStartTime,
              })
            )
          } catch {
            // Don't fail on hook errors
          }

          break
        }

        // Execute a turn
        const turnResult = await this.executeTurn(
          client,
          history,
          tools,
          inputs.cwd,
          auth.type,
          combinedSignal
        )

        if (turnResult.status === 'stop') {
          terminateMode = turnResult.terminateMode
          finalResult = turnResult.result
          break
        }

        // Check for doom loop after turn completes
        if (this.doomLoopDetected) {
          terminateMode = AgentTerminateMode.DOOM_LOOP

          // Emit doom loop event
          this.emit({
            type: 'error',
            agentId: this.agentId,
            timestamp: Date.now(),
            error:
              'Doom loop detected: Agent is repeating the same action. Please provide guidance or change approach.',
          })

          // Attempt recovery with doom loop context
          history.push({
            role: 'user',
            content: `DOOM LOOP DETECTED: You have called the same tool with identical arguments ${DOOM_LOOP_THRESHOLD} times in a row. This suggests you may be stuck. Please:
1. Analyze why the tool is not giving the expected result
2. Try a different approach or tool
3. If you have completed the task, call \`complete_task\`
4. If you are stuck, explain what you're trying to do

Do not repeat the same action again.`,
          })

          // Reset doom loop flag and give one more chance
          this.doomLoopDetected = false
          this.toolCallHistory = []
        }

        // Continue to next turn - tool results already added to history in executeTurn
      }

      // Attempt recovery for recoverable failures
      if (this.isRecoverable(terminateMode)) {
        const recoveryResult = await this.attemptRecovery(
          client,
          history,
          tools,
          inputs.cwd,
          auth.type,
          terminateMode,
          signal
        )

        if (recoveryResult.success) {
          terminateMode = AgentTerminateMode.GOAL
          finalResult = recoveryResult.result
        } else {
          // Set appropriate error message
          finalResult = this.getTerminationMessage(terminateMode)
        }
      }

      // Build final result
      const success = terminateMode === AgentTerminateMode.GOAL
      return {
        success,
        terminateMode,
        output:
          finalResult ?? (success ? 'Task completed.' : 'Agent terminated before completion.'),
        steps: this.steps,
        tokensUsed: this.tokensUsed,
        durationMs: Date.now() - startTime,
        turns: this.turnCounter,
        error: success ? undefined : (finalResult ?? 'Unknown error'),
      }
    } catch (error) {
      // Handle abort errors
      if (error instanceof Error && error.name === 'AbortError') {
        terminateMode = timeoutController.signal.aborted
          ? AgentTerminateMode.TIMEOUT
          : AgentTerminateMode.ABORTED
      }

      this.emit({
        type: 'error',
        agentId: this.agentId,
        timestamp: Date.now(),
        error: String(error),
      })

      return {
        success: false,
        terminateMode: AgentTerminateMode.ERROR,
        output: `Agent error: ${error instanceof Error ? error.message : String(error)}`,
        steps: this.steps,
        tokensUsed: this.tokensUsed,
        durationMs: Date.now() - startTime,
        turns: this.turnCounter,
        error: String(error),
      }
    } finally {
      clearTimeout(timeoutId)

      // Emit finish event
      const result: AgentResult = {
        success: terminateMode === AgentTerminateMode.GOAL,
        terminateMode,
        output: finalResult ?? '',
        steps: this.steps,
        tokensUsed: this.tokensUsed,
        durationMs: Date.now() - startTime,
        turns: this.turnCounter,
      }

      this.emit({
        type: 'agent:finish',
        agentId: this.agentId,
        timestamp: Date.now(),
        result,
      })
    }
  }

  /**
   * Execute a single turn of the agent loop
   */
  private async executeTurn(
    client: Awaited<ReturnType<typeof createClient>>,
    history: ChatMessage[],
    tools: ToolDefinition[],
    cwd: string,
    authMethod: 'api-key' | 'oauth',
    signal: AbortSignal
  ): Promise<AgentTurnResult> {
    const turnNumber = this.turnCounter++

    this.emit({
      type: 'turn:start',
      agentId: this.agentId,
      timestamp: Date.now(),
      turn: turnNumber,
    })

    // Reset tool call counter for this turn
    resetToolCallCount()

    // Call LLM
    let assistantContent = ''
    const toolCalls: Array<{ id: string; name: string; arguments: Record<string, unknown> }> = []

    const stream = client.stream(
      history,
      {
        provider: this.config.provider ?? 'anthropic',
        model: this.config.model ?? 'claude-sonnet-4-20250514',
        authMethod,
        tools,
      },
      signal
    )

    for await (const delta of stream) {
      if (signal.aborted) break

      // Accumulate content
      if (delta.content) {
        assistantContent += delta.content

        // Emit thoughts
        if (delta.content.trim()) {
          this.emit({
            type: 'thought',
            agentId: this.agentId,
            timestamp: Date.now(),
            text: delta.content,
          })
        }
      }

      // Collect tool calls
      if (delta.toolUse) {
        toolCalls.push({
          id: delta.toolUse.id,
          name: delta.toolUse.name,
          arguments: delta.toolUse.input ?? {},
        })
      }

      // Track token usage
      if (delta.usage) {
        this.tokensUsed += (delta.usage.inputTokens ?? 0) + (delta.usage.outputTokens ?? 0)
      }
    }

    // Add assistant message to history
    if (assistantContent || toolCalls.length > 0) {
      // Format tool calls into message if present
      let content = assistantContent
      if (toolCalls.length > 0) {
        const toolCallsText = toolCalls
          .map((tc) => `[Tool Call: ${tc.name}]\n${JSON.stringify(tc.arguments, null, 2)}`)
          .join('\n\n')
        content = assistantContent ? `${assistantContent}\n\n${toolCallsText}` : toolCallsText
      }
      history.push({ role: 'assistant', content })
    }

    // If no tool calls, check if we should stop
    if (toolCalls.length === 0) {
      this.emit({
        type: 'turn:finish',
        agentId: this.agentId,
        timestamp: Date.now(),
        turn: turnNumber,
        toolCalls: [],
      })

      // Agent stopped without calling complete_task
      return {
        status: 'stop',
        terminateMode: AgentTerminateMode.NO_COMPLETE_TASK,
        result: null,
      }
    }

    // Process tool calls
    const toolCallInfos: ToolCallInfo[] = []
    const toolResults: string[] = []
    let taskCompleted = false
    let submittedOutput: string | null = null

    const toolContext: ToolContext = {
      sessionId: this.sessionId,
      workingDirectory: cwd,
      signal,
      metadata: (update) => {
        // Emit tool:metadata event for streaming updates
        this.emit({
          type: 'tool:metadata',
          agentId: this.agentId,
          timestamp: Date.now(),
          toolName: toolCalls[0]?.name ?? 'unknown',
          title: update.title,
          metadata: update.metadata,
        })
      },
    }

    for (const call of toolCalls) {
      const startTime = Date.now()

      this.emit({
        type: 'tool:start',
        agentId: this.agentId,
        timestamp: Date.now(),
        toolName: call.name,
        args: call.arguments,
      })

      // Handle complete_task specially
      if (call.name === COMPLETE_TASK_TOOL) {
        taskCompleted = true
        const args = call.arguments as { result?: unknown; command?: unknown }
        const resultArg = args.result
        const commandArg = args.command
        submittedOutput =
          typeof resultArg === 'string' ? resultArg : JSON.stringify(resultArg, null, 2)

        // Run TaskComplete hook
        try {
          const hookRunner = getHookRunner(cwd)
          await hookRunner.run(
            'TaskComplete',
            createTaskCompleteContext({
              success: true,
              output: submittedOutput,
              command: typeof commandArg === 'string' ? commandArg : undefined,
              sessionId: this.sessionId,
              workingDirectory: cwd,
              durationMs: Date.now() - this.taskStartTime,
              toolCallCount: this.totalToolCalls + getToolCallCount(),
            })
          )
        } catch {
          // Don't fail on hook errors
        }

        toolResults.push(`[Tool Result: ${call.id}]\nTask marked as complete.`)

        toolCallInfos.push({
          name: call.name,
          args: call.arguments,
          result: 'Task marked as complete.',
          success: true,
          durationMs: Date.now() - startTime,
        })

        this.emit({
          type: 'tool:finish',
          agentId: this.agentId,
          timestamp: Date.now(),
          toolName: call.name,
          success: true,
          output: 'Task marked as complete.',
          durationMs: Date.now() - startTime,
        })

        continue
      }

      // Execute regular tool
      const result = await executeTool(call.name, call.arguments, toolContext)
      const durationMs = Date.now() - startTime

      // Track tool call for doom loop detection
      this.toolCallHistory.push({
        name: call.name,
        argsHash: hashArgs(call.arguments),
        turn: turnNumber,
      })

      // Keep only last 10 calls to limit memory
      if (this.toolCallHistory.length > 10) {
        this.toolCallHistory.shift()
      }

      // Check for doom loop
      if (detectDoomLoop(this.toolCallHistory)) {
        this.doomLoopDetected = true
      }

      toolResults.push(`[Tool Result: ${call.id}]\n${result.output}`)

      toolCallInfos.push({
        name: call.name,
        args: call.arguments,
        result: result.output,
        success: result.success,
        durationMs,
      })

      if (result.success) {
        this.emit({
          type: 'tool:finish',
          agentId: this.agentId,
          timestamp: Date.now(),
          toolName: call.name,
          success: true,
          output: result.output,
          durationMs,
        })
      } else {
        this.emit({
          type: 'tool:error',
          agentId: this.agentId,
          timestamp: Date.now(),
          toolName: call.name,
          error: result.error ?? result.output,
        })
      }
    }

    // Add tool results to history as user message
    if (toolResults.length > 0) {
      history.push({
        role: 'user',
        content: toolResults.join('\n\n'),
      })
    }

    // Record step
    this.steps.push({
      id: `step-${turnNumber}`,
      turn: turnNumber,
      description: assistantContent.slice(0, 100) || 'Tool execution',
      toolsCalled: toolCallInfos,
      status: taskCompleted ? 'success' : 'running',
      output: toolCallInfos.map((t) => t.result).join('\n'),
      retryCount: 0,
      startedAt: Date.now() - toolCallInfos.reduce((acc, t) => acc + (t.durationMs ?? 0), 0),
      completedAt: Date.now(),
    })

    // Track total tool calls
    this.totalToolCalls += toolCallInfos.length

    this.emit({
      type: 'turn:finish',
      agentId: this.agentId,
      timestamp: Date.now(),
      turn: turnNumber,
      toolCalls: toolCallInfos,
    })

    if (taskCompleted) {
      return {
        status: 'stop',
        terminateMode: AgentTerminateMode.GOAL,
        result: submittedOutput,
      }
    }

    return {
      status: 'continue',
      toolCalls: toolCallInfos,
    }
  }

  /**
   * Attempt recovery after a recoverable failure
   */
  private async attemptRecovery(
    client: Awaited<ReturnType<typeof createClient>>,
    history: ChatMessage[],
    tools: ToolDefinition[],
    cwd: string,
    authMethod: 'api-key' | 'oauth',
    reason: AgentTerminateMode,
    externalSignal: AbortSignal
  ): Promise<{ success: boolean; result: string | null }> {
    const startTime = Date.now()

    this.emit({
      type: 'recovery:start',
      agentId: this.agentId,
      timestamp: Date.now(),
      reason,
      turn: this.turnCounter,
    })

    // Create grace period timeout
    const graceController = new AbortController()
    const graceTimeout = setTimeout(
      () => graceController.abort(new Error('Grace period expired')),
      this.config.gracePeriodMs
    )

    try {
      // Add recovery message to history
      const warningMessage = this.getRecoveryMessage(reason)
      history.push({ role: 'user', content: warningMessage })

      const combinedSignal = AbortSignal.any([externalSignal, graceController.signal])

      const turnResult = await this.executeTurn(
        client,
        history,
        tools,
        cwd,
        authMethod,
        combinedSignal
      )

      const success =
        turnResult.status === 'stop' && turnResult.terminateMode === AgentTerminateMode.GOAL

      this.emit({
        type: 'recovery:finish',
        agentId: this.agentId,
        timestamp: Date.now(),
        success,
        durationMs: Date.now() - startTime,
      })

      if (success) {
        return { success: true, result: turnResult.result }
      }

      return { success: false, result: null }
    } catch {
      this.emit({
        type: 'recovery:finish',
        agentId: this.agentId,
        timestamp: Date.now(),
        success: false,
        durationMs: Date.now() - startTime,
      })

      return { success: false, result: null }
    } finally {
      clearTimeout(graceTimeout)
    }
  }

  /**
   * Build the system prompt for the agent
   */
  private buildSystemPrompt(inputs: AgentInputs): string {
    let prompt = `You are an autonomous agent executing tasks. Your goal is to complete the user's request using the available tools.

# Environment
- Working directory: ${inputs.cwd}
- Current date: ${new Date().toLocaleDateString()}

# Available Tools
You have access to various tools for file operations, code execution, and more. Use them systematically to accomplish your goal.

# Rules
1. You are running autonomously - you CANNOT ask the user for input or clarification.
2. Work systematically, break down complex tasks into steps.
3. Always use absolute paths for file operations.
4. When you have completed your task, you MUST call the \`${COMPLETE_TASK_TOOL}\` tool with your findings.
5. The \`${COMPLETE_TASK_TOOL}\` tool is the ONLY way to finish. If you stop calling tools without calling it, you have failed.
6. Include comprehensive results in the "result" parameter of \`${COMPLETE_TASK_TOOL}\`.
7. Do not call any other tools in the same turn as \`${COMPLETE_TASK_TOOL}\`.

# Context`

    if (inputs.context) {
      prompt += `\n${inputs.context}`
    }

    return prompt
  }

  /**
   * Get available tools (filtered by config if specified)
   */
  private getAvailableTools(): ToolDefinition[] {
    let tools = getToolDefinitions()

    // Filter by configured tools if specified
    if (this.config.tools && this.config.tools.length > 0) {
      const allowedTools = new Set(this.config.tools)
      tools = tools.filter((t) => allowedTools.has(t.name))
    }

    // Add complete_task tool
    const completeTaskTool: ToolDefinition = {
      name: COMPLETE_TASK_TOOL,
      description:
        'Call this tool to submit your final findings and complete the task. This is the ONLY way to finish.',
      input_schema: {
        type: 'object',
        properties: {
          result: {
            type: 'string',
            description:
              'Your final results or findings. Ensure this is comprehensive and follows any formatting requested.',
          },
        },
        required: ['result'],
      },
    }

    return [...tools, completeTaskTool]
  }

  /**
   * Check if the agent should terminate
   */
  private checkTermination(startTime: number): AgentTerminateMode | null {
    // Check max turns
    if (this.turnCounter >= this.config.maxTurns) {
      return AgentTerminateMode.MAX_TURNS
    }

    // Check timeout (with small buffer)
    const elapsedMs = Date.now() - startTime
    const timeoutMs = this.config.maxTimeMinutes * 60 * 1000
    if (elapsedMs >= timeoutMs - 5000) {
      return AgentTerminateMode.TIMEOUT
    }

    return null
  }

  /**
   * Check if a termination mode is recoverable
   */
  private isRecoverable(mode: AgentTerminateMode): boolean {
    return (
      mode === AgentTerminateMode.TIMEOUT ||
      mode === AgentTerminateMode.MAX_TURNS ||
      mode === AgentTerminateMode.NO_COMPLETE_TASK
    )
  }

  /**
   * Get recovery message for a termination reason
   */
  private getRecoveryMessage(reason: AgentTerminateMode): string {
    let explanation = ''
    switch (reason) {
      case AgentTerminateMode.TIMEOUT:
        explanation = 'You have exceeded the time limit.'
        break
      case AgentTerminateMode.MAX_TURNS:
        explanation = 'You have exceeded the maximum number of turns.'
        break
      case AgentTerminateMode.NO_COMPLETE_TASK:
        explanation = 'You stopped calling tools without finishing.'
        break
      default:
        explanation = 'Execution was interrupted.'
    }

    return `${explanation} You have one final chance to complete the task with a short grace period. You MUST call \`${COMPLETE_TASK_TOOL}\` immediately with your best answer. Do not call any other tools.`
  }

  /**
   * Get termination message for a mode
   */
  private getTerminationMessage(mode: AgentTerminateMode): string {
    switch (mode) {
      case AgentTerminateMode.TIMEOUT:
        return `Agent timed out after ${this.config.maxTimeMinutes} minutes.`
      case AgentTerminateMode.MAX_TURNS:
        return `Agent reached maximum turn limit (${this.config.maxTurns}).`
      case AgentTerminateMode.NO_COMPLETE_TASK:
        return 'Agent stopped without calling complete_task.'
      case AgentTerminateMode.ABORTED:
        return 'Agent was aborted.'
      default:
        return 'Agent terminated unexpectedly.'
    }
  }

  /**
   * Emit an event
   */
  private emit(event: AgentEvent): void {
    if (this.onEvent) {
      try {
        this.onEvent(event)
      } catch {
        // Ignore listener errors
      }
    }
  }
}

// ============================================================================
// Factory Function
// ============================================================================

/**
 * Create and run an agent
 *
 * @param inputs - Agent inputs including goal
 * @param config - Agent configuration
 * @param signal - AbortSignal for cancellation
 * @param onEvent - Optional event callback
 * @returns Agent result
 */
export async function runAgent(
  inputs: AgentInputs,
  config: Partial<AgentConfig>,
  signal: AbortSignal,
  onEvent?: AgentEventCallback
): Promise<AgentResult> {
  const executor = new AgentExecutor(config, onEvent)
  return executor.run(inputs, signal)
}
