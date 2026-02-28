/**
 * Task tool — spawn a subagent for focused subtasks.
 *
 * Synchronous: parent blocks until child completes.
 * Child cannot call 'task' (prevents recursion).
 */

import { AgentExecutor } from '@ava/core-v2/agent'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

const BUILTIN_WORKERS: Record<
  string,
  { tools: string[]; systemPrompt: string; maxTurns: number; maxTimeMinutes: number }
> = {
  coder: {
    tools: ['read_file', 'write_file', 'create_file', 'delete_file', 'edit', 'grep', 'glob'],
    systemPrompt:
      'You are a senior developer. Write clean, well-structured code. Focus on the task, make minimal changes, and follow existing patterns.',
    maxTurns: 15,
    maxTimeMinutes: 5,
  },
  tester: {
    tools: ['read_file', 'write_file', 'create_file', 'bash', 'grep', 'glob'],
    systemPrompt:
      'You are a QA engineer. Write comprehensive tests covering happy paths, edge cases, and error cases. Run tests to verify they pass.',
    maxTurns: 10,
    maxTimeMinutes: 5,
  },
  reviewer: {
    tools: ['read_file', 'grep', 'glob'],
    systemPrompt:
      'You are a code reviewer. Analyze code for bugs, security issues, and quality. You have read-only access.',
    maxTurns: 10,
    maxTimeMinutes: 5,
  },
  researcher: {
    tools: ['read_file', 'grep', 'glob', 'ls'],
    systemPrompt:
      'You are a codebase researcher. Explore the codebase to gather context and understand architecture. You have read-only access.',
    maxTurns: 15,
    maxTimeMinutes: 5,
  },
  debugger: {
    tools: ['read_file', 'write_file', 'edit', 'bash', 'grep', 'glob'],
    systemPrompt:
      'You are a debugging specialist. Diagnose issues, trace errors, and apply fixes. Be methodical and verify fixes work.',
    maxTurns: 15,
    maxTimeMinutes: 5,
  },
}

export const taskTool = defineTool({
  name: 'task',
  description:
    'Spawn a subagent for a focused subtask. The subagent runs with its own conversation history and a filtered set of tools. Use this to delegate work like research, code review, or focused edits.',
  schema: z.object({
    description: z.string().describe('Short task description (3-5 words)'),
    prompt: z.string().describe('Full instructions for the subagent'),
    worker: z
      .enum(['coder', 'tester', 'reviewer', 'researcher', 'debugger'])
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
  }),
  async execute(input, ctx) {
    const workerDef = input.worker ? BUILTIN_WORKERS[input.worker] : undefined
    const allowedTools = input.allowedTools ?? workerDef?.tools ?? ['read_file', 'grep', 'glob']

    // Filter out 'task' to prevent infinite recursion
    const filtered = allowedTools.filter((t) => t !== 'task')

    const child = new AgentExecutor({
      provider: ctx.provider as import('@ava/core-v2/llm').LLMProvider | undefined,
      model: ctx.model,
      allowedTools: filtered,
      maxTurns: input.maxTurns ?? workerDef?.maxTurns ?? 15,
      maxTimeMinutes: workerDef?.maxTimeMinutes ?? 5,
      systemPrompt: workerDef?.systemPrompt,
      name: `subagent:${input.description}`,
    })

    const result = await child.run({ goal: input.prompt, cwd: ctx.workingDirectory }, ctx.signal)

    return {
      success: result.success,
      output: result.output || `Subagent finished (${result.terminateMode})`,
    }
  },
})
