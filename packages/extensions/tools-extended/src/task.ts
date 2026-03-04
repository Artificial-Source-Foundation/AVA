/**
 * Task tool — spawn a subagent for focused subtasks.
 *
 * Synchronous: parent blocks until child completes.
 * Child cannot call 'task' (prevents recursion).
 * Supports resumption via task_id (loads existing session history).
 */

import type { AgentEventCallback } from '@ava/core-v2/agent'
import { AgentExecutor, registerExecutor, unregisterExecutor } from '@ava/core-v2/agent'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'
import { WORKER_AGENTS } from '../../commander/src/workers.js'

// Convert array to record for lookup
const BUILTIN_WORKERS = Object.fromEntries(
  WORKER_AGENTS.map((w) => [
    w.name,
    {
      tools: w.tools,
      systemPrompt: w.systemPrompt,
      maxTurns: w.maxTurns ?? 15,
      maxTimeMinutes: w.maxTimeMinutes ?? 5,
    },
  ])
) as Record<
  string,
  { tools: string[]; systemPrompt: string; maxTurns: number; maxTimeMinutes: number }
>

export const taskTool = defineTool({
  name: 'task',
  description: 'Spawn a subagent for a focused subtask. Pass task_id to resume a previous session.',
  schema: z.object({
    description: z.string().describe('Short task description (3-5 words)'),
    prompt: z.string().describe('Full instructions for the subagent'),
    worker: z
      .enum([
        'coder',
        'tester',
        'reviewer',
        'researcher',
        'debugger',
        'architect',
        'planner',
        'devops',
        'explorer',
      ])
      .optional()
      .describe('Worker type — determines available tools and system prompt'),
    allowedTools: z
      .array(z.string())
      .optional()
      .describe('Override: specific tools the subagent can use'),
    maxTurns: z
      .number()
      .int()
      .min(1)
      .max(30)
      .optional()
      .describe('Maximum turns for subagent (default: 15)'),
    task_id: z
      .string()
      .optional()
      .describe(
        'Resume an existing subagent session by its ID. The prompt becomes a follow-up message.'
      ),
  }),
  async execute(input, ctx) {
    const workerDef = input.worker ? BUILTIN_WORKERS[input.worker] : undefined
    const allowedTools = input.allowedTools ?? workerDef?.tools ?? ['read_file', 'grep', 'glob']

    // Allow 'task' tool in children up to a depth limit (prevents infinite recursion)
    const currentDepth = ctx.delegationDepth ?? 0
    const maxTaskDepth = 3
    const filtered =
      currentDepth >= maxTaskDepth ? allowedTools.filter((t) => t !== 'task') : allowedTools

    const child = new AgentExecutor(
      {
        provider: ctx.provider as import('@ava/core-v2/llm').LLMProvider | undefined,
        model: ctx.model,
        allowedTools: filtered,
        maxTurns: input.maxTurns ?? workerDef?.maxTurns ?? 15,
        maxTimeMinutes: workerDef?.maxTimeMinutes ?? 5,
        systemPrompt: workerDef?.systemPrompt,
        name: `subagent:${input.description}`,
        delegationDepth: currentDepth + 1,
      },
      ctx.onEvent as AgentEventCallback | undefined
    )

    // Register child executor for UI stop operations
    const childAbort = new AbortController()
    registerExecutor(child.agentId, child, childAbort, ctx.sessionId, input.description)

    try {
      const combinedSignal = ctx.signal
        ? AbortSignal.any([ctx.signal, childAbort.signal])
        : childAbort.signal
      const result = await child.run(
        {
          goal: input.prompt,
          cwd: ctx.workingDirectory,
          sessionId: input.task_id,
        },
        combinedSignal
      )

      return {
        success: result.success,
        output: result.output || `Subagent finished (${result.terminateMode})`,
        metadata: {
          taskId: child.agentId,
          terminateMode: result.terminateMode,
          turns: result.turns,
        },
      }
    } finally {
      unregisterExecutor(child.agentId)
    }
  },
})
