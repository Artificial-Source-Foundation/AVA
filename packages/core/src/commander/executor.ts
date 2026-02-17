/**
 * Worker Executor
 * Executes workers with isolated tool access and recursion prevention
 *
 * Based on Gemini CLI's local-executor.ts pattern
 */

import { AgentExecutor } from '../agent/loop.js'
import type { AgentEvent, AgentEventCallback, AgentResult } from '../agent/types.js'
import { AgentTerminateMode } from '../agent/types.js'
import { getEditorModelConfig } from '../llm/client.js'
import type { WorkerRegistry } from './registry.js'
import { analyzeTask, selectWorker } from './router.js'
import type {
  WorkerActivityCallback,
  WorkerActivityEvent,
  WorkerDefinition,
  WorkerInputs,
  WorkerResult,
} from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Prefix for worker delegation tools */
export const DELEGATE_TOOL_PREFIX = 'delegate_'

/** Default configuration for workers */
const DEFAULT_WORKER_CONFIG = {
  maxTurns: 10,
  maxTimeMinutes: 5,
  maxRetries: 2,
  gracePeriodMs: 30 * 1000, // 30 seconds
}

// ============================================================================
// Worker Executor
// ============================================================================

/**
 * Execute a worker with isolated tool access
 *
 * Key features:
 * - Filters tools to only those allowed for the worker
 * - CRITICAL: Blocks all delegate_* tools (recursion prevention)
 * - Creates isolated AgentExecutor instance
 * - Streams activity events to callback
 *
 * @param definition - Worker definition
 * @param inputs - Worker inputs (task, context, cwd)
 * @param signal - AbortSignal for cancellation
 * @param onActivity - Optional callback for activity events
 * @returns Worker result
 */
export async function executeWorker(
  definition: WorkerDefinition,
  inputs: WorkerInputs,
  signal: AbortSignal,
  onActivity?: WorkerActivityCallback
): Promise<WorkerResult> {
  const startTime = Date.now()

  // Emit worker start event
  emitActivity(onActivity, {
    type: 'progress',
    workerName: definition.name,
    timestamp: startTime,
    data: {
      status: 'starting',
      task: inputs.task,
    },
  })

  try {
    // Get filtered tool list
    // CRITICAL: This prevents workers from calling other workers (recursion prevention)
    const allowedTools = getFilteredTools(definition.tools)

    // Build worker-specific system prompt
    const workerContext = buildWorkerContext(definition, inputs)

    // Create event callback that bridges to activity callback
    const eventCallback: AgentEventCallback | undefined = onActivity
      ? createEventBridge(definition.name, onActivity)
      : undefined

    // Resolve editor model for workers without explicit model override
    // (architect/editor split: Team Lead uses primary model, workers use editor model)
    const editorConfig = getEditorModelConfig()
    const workerModel = definition.model ?? editorConfig.model
    const workerProvider =
      definition.provider ?? (editorConfig.provider as 'anthropic' | 'openai' | 'openrouter')

    // Create isolated agent executor for this worker
    const executor = new AgentExecutor(
      {
        id: `worker-${definition.name}-${Date.now()}`,
        name: definition.displayName,
        maxTurns: definition.maxTurns ?? DEFAULT_WORKER_CONFIG.maxTurns,
        maxTimeMinutes: definition.maxTimeMinutes ?? DEFAULT_WORKER_CONFIG.maxTimeMinutes,
        maxRetries: DEFAULT_WORKER_CONFIG.maxRetries,
        gracePeriodMs: DEFAULT_WORKER_CONFIG.gracePeriodMs,
        tools: allowedTools,
        model: workerModel,
        provider: workerProvider,
      },
      eventCallback
    )

    // Execute the worker
    const result = await executor.run(
      {
        goal: inputs.task,
        context: workerContext,
        cwd: inputs.cwd,
      },
      signal
    )

    // Convert agent result to worker result
    return convertToWorkerResult(result)
  } catch (error) {
    // Handle execution errors
    const errorMessage = error instanceof Error ? error.message : String(error)

    emitActivity(onActivity, {
      type: 'error',
      workerName: definition.name,
      timestamp: Date.now(),
      data: {
        error: errorMessage,
      },
    })

    return {
      success: false,
      output: `Worker error: ${errorMessage}`,
      terminateMode: AgentTerminateMode.ERROR,
      tokensUsed: 0,
      durationMs: Date.now() - startTime,
      turns: 0,
      error: errorMessage,
    }
  }
}

// ============================================================================
// Tool Filtering
// ============================================================================

/**
 * Get filtered tools for a worker
 *
 * CRITICAL: This is the recursion prevention mechanism.
 * Workers cannot call delegate_* tools, preventing infinite delegation chains.
 *
 * @param allowedToolNames - List of tool names the worker can use
 * @returns Filtered list excluding delegate_* tools
 */
export function getFilteredTools(allowedToolNames: string[]): string[] {
  return allowedToolNames.filter((toolName) => {
    // CRITICAL: Prevent recursion - workers cannot call delegate_* tools
    if (toolName.startsWith(DELEGATE_TOOL_PREFIX)) {
      return false
    }
    return true
  })
}

/**
 * Check if a tool name is a delegation tool
 */
export function isDelegationTool(toolName: string): boolean {
  return toolName.startsWith(DELEGATE_TOOL_PREFIX)
}

// ============================================================================
// Context Building
// ============================================================================

/**
 * Build the worker context from definition and inputs
 */
function buildWorkerContext(definition: WorkerDefinition, inputs: WorkerInputs): string {
  const contextParts: string[] = []

  // Add worker system prompt
  contextParts.push(definition.systemPrompt)

  // Add parent context if provided
  if (inputs.context) {
    contextParts.push('\n# Additional Context')
    contextParts.push(inputs.context)
  }

  // Add parent tracking info if provided
  if (inputs.parentAgentId) {
    contextParts.push(`\n# Parent Agent: ${inputs.parentAgentId}`)
  }

  return contextParts.join('\n')
}

// ============================================================================
// Event Bridging
// ============================================================================

/**
 * Create an event callback that bridges agent events to worker activity events
 */
function createEventBridge(
  workerName: string,
  onActivity: WorkerActivityCallback
): AgentEventCallback {
  return (event: AgentEvent): void => {
    const activityEvent = convertAgentEventToActivity(workerName, event)
    if (activityEvent) {
      onActivity(activityEvent)
    }
  }
}

/**
 * Convert an agent event to a worker activity event
 */
function convertAgentEventToActivity(
  workerName: string,
  event: AgentEvent
): WorkerActivityEvent | null {
  switch (event.type) {
    case 'thought':
      return {
        type: 'thought',
        workerName,
        timestamp: event.timestamp,
        data: { text: event.text },
      }

    case 'tool:start':
      return {
        type: 'tool:start',
        workerName,
        timestamp: event.timestamp,
        data: {
          toolName: event.toolName,
          args: event.args,
        },
      }

    case 'tool:finish':
      return {
        type: 'tool:finish',
        workerName,
        timestamp: event.timestamp,
        data: {
          toolName: event.toolName,
          success: event.success,
          output: event.output,
          durationMs: event.durationMs,
        },
      }

    case 'tool:error':
      return {
        type: 'tool:error',
        workerName,
        timestamp: event.timestamp,
        data: {
          toolName: event.toolName,
          error: event.error,
        },
      }

    case 'error':
      return {
        type: 'error',
        workerName,
        timestamp: event.timestamp,
        data: {
          error: event.error,
          context: event.context,
        },
      }

    case 'turn:start':
    case 'turn:finish':
      return {
        type: 'progress',
        workerName,
        timestamp: event.timestamp,
        data: {
          event: event.type,
          turn: event.turn,
        },
      }

    case 'agent:start':
    case 'agent:finish':
    case 'recovery:start':
    case 'recovery:finish':
      return {
        type: 'progress',
        workerName,
        timestamp: event.timestamp,
        data: {
          event: event.type,
        },
      }

    default:
      return null
  }
}

/**
 * Helper to emit activity events
 */
function emitActivity(
  onActivity: WorkerActivityCallback | undefined,
  event: WorkerActivityEvent
): void {
  if (onActivity) {
    try {
      onActivity(event)
    } catch {
      // Ignore callback errors
    }
  }
}

// ============================================================================
// Result Conversion
// ============================================================================

/**
 * Convert an AgentResult to a WorkerResult
 */
function convertToWorkerResult(result: AgentResult): WorkerResult {
  return {
    success: result.success,
    output: result.output,
    terminateMode: result.terminateMode,
    tokensUsed: result.tokensUsed,
    durationMs: result.durationMs,
    turns: result.turns,
    error: result.error,
    steps: result.steps,
  }
}

// ============================================================================
// Auto-Routing
// ============================================================================

/**
 * Try to auto-route a task to the best worker based on keyword analysis.
 * Returns null if confidence is too low — caller should fall back to LLM routing.
 *
 * @param goal - Task description
 * @param registry - Worker registry
 * @param signal - AbortSignal for cancellation
 * @param cwd - Working directory
 * @param onActivity - Optional activity callback
 * @param minConfidence - Minimum confidence threshold (default: 0.7)
 */
export async function executeWithAutoRouting(
  goal: string,
  registry: WorkerRegistry,
  signal: AbortSignal,
  cwd: string,
  onActivity?: WorkerActivityCallback,
  minConfidence = 0.7
): Promise<WorkerResult | null> {
  const analysis = analyzeTask(goal)
  if (analysis.confidence < minConfidence) {
    return null
  }

  const worker = selectWorker(analysis, registry)
  if (!worker) {
    return null
  }

  return executeWorker(worker, { task: goal, cwd }, signal, onActivity)
}
