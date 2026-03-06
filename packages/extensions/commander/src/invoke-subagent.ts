import { AgentExecutor, registerExecutor, unregisterExecutor } from '@ava/core-v2/agent'
import type { LLMProvider } from '@ava/core-v2/llm'
import type { AnyTool, ToolContext, ToolResult } from '@ava/core-v2/tools'
import { getTierPrompt } from './tier-prompts.js'

export interface InvokeSubagentInput {
  type: 'explore' | 'reviewer' | 'research' | 'analyze'
  task: string
  context?: string
  run_validation?: boolean
  changed_files?: string[]
}

export interface InvokeSubagentResult {
  success: boolean
  output: string
  approved?: boolean
  feedback?: string
  lintPassed?: boolean
  testsPassed?: boolean
  typecheckPassed?: boolean
}

const READ_ONLY_TOOLS = ['read_file', 'glob', 'grep', 'attempt_completion']
const REVIEWER_TOOLS = ['read_file', 'glob', 'grep', 'bash', 'attempt_completion']

function buildSubagentGoal(params: InvokeSubagentInput): string {
  const context = params.context ? `\n\nContext:\n${params.context}` : ''
  if (params.type !== 'reviewer' || !params.run_validation) {
    return `${params.task}${context}`
  }

  const changed = params.changed_files?.length ? params.changed_files.join(' ') : '<changed-files>'

  return `${params.task}${context}\n\nValidation checklist:\n1. npx biome check ${changed}\n2. npx tsc --noEmit\n3. npx vitest <test-files>\n4. Review diff correctness and conventions\n5. Return approved boolean and feedback.`
}

export function createInvokeSubagentTool(): AnyTool {
  return {
    definition: {
      name: 'invoke_subagent',
      description: 'Invoke an ephemeral helper agent (explore, reviewer, research, analyze).',
      input_schema: {
        type: 'object',
        properties: {
          type: { type: 'string', enum: ['explore', 'reviewer', 'research', 'analyze'] },
          task: { type: 'string' },
          context: { type: 'string' },
          run_validation: { type: 'boolean' },
          changed_files: { type: 'array', items: { type: 'string' } },
        },
        required: ['type', 'task'],
      },
    },

    async execute(params: InvokeSubagentInput, ctx: ToolContext): Promise<ToolResult> {
      const role = params.type === 'reviewer' ? 'reviewer' : 'subagent'
      const allowedTools = params.type === 'reviewer' ? REVIEWER_TOOLS : READ_ONLY_TOOLS
      const model =
        params.type === 'reviewer' ? 'anthropic/claude-sonnet-4-6' : 'anthropic/claude-haiku-4-5'
      const childId = crypto.randomUUID()

      ctx.onEvent?.({
        type: 'praxis:review-requested',
        agentId: ctx.sessionId,
        subagentId: childId,
        subagentType: params.type,
      })

      const child = new AgentExecutor(
        {
          id: childId,
          name: `subagent:${params.type}`,
          provider: (ctx.provider as LLMProvider | undefined) ?? 'openrouter',
          model,
          allowedTools,
          maxTurns: params.type === 'reviewer' ? 8 : 6,
          maxTimeMinutes: 6,
          systemPrompt: getTierPrompt(role),
          delegationDepth: (ctx.delegationDepth ?? 0) + 1,
        },
        ctx.onEvent
      )

      const abort = new AbortController()
      registerExecutor(childId, child, abort, ctx.sessionId, `subagent:${params.type}`)

      try {
        const signal = AbortSignal.any([ctx.signal, abort.signal])
        const result = await child.run(
          { goal: buildSubagentGoal(params), cwd: ctx.workingDirectory },
          signal
        )

        const reviewerMeta: InvokeSubagentResult = {
          success: result.success,
          output: result.output,
        }

        if (params.type === 'reviewer') {
          const outputLower = result.output.toLowerCase()
          reviewerMeta.approved = /approved\s*[:=]\s*true/.test(outputLower)
          reviewerMeta.feedback = result.output
          reviewerMeta.lintPassed = !outputLower.includes('lint failed')
          reviewerMeta.typecheckPassed = !outputLower.includes('typecheck failed')
          reviewerMeta.testsPassed = !outputLower.includes('test failed')
        }

        ctx.onEvent?.({
          type: 'praxis:review-complete',
          agentId: ctx.sessionId,
          subagentId: childId,
          success: result.success,
          approved: reviewerMeta.approved,
        })

        return {
          success: result.success,
          output: reviewerMeta.output,
          metadata: reviewerMeta as unknown as Record<string, unknown>,
        }
      } finally {
        unregisterExecutor(childId)
      }
    },
  }
}
