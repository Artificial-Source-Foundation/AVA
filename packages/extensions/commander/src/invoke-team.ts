import { AgentExecutor, registerExecutor, unregisterExecutor } from '@ava/core-v2/agent'
import type { LLMProvider } from '@ava/core-v2/llm'
import type { AnyTool, ToolContext, ToolResult } from '@ava/core-v2/tools'
import { createWorktree, removeWorktree } from '../../git/src/worktree.js'
import { applyTierToolPolicy } from './orchestrator.js'
import { getTierPrompt } from './tier-prompts.js'

export interface InvokeTeamInput {
  role: 'tech-lead' | 'engineer'
  task: string
  domain?: string
  files?: string[]
  context?: string
  worktree?: boolean
}

export interface InvokeTeamResult {
  agentId: string
  success: boolean
  summary: string
  filesChanged: string[]
  worktreeBranch?: string
}

function parseChangedFiles(output: string): string[] {
  const matches = output.match(/([\w./-]+\.(?:ts|tsx|js|jsx|json|md|rs))/g) ?? []
  return [...new Set(matches)]
}

function buildGoal(params: InvokeTeamInput): string {
  const lines = [params.task]
  if (params.domain) lines.push(`Domain: ${params.domain}`)
  if (params.files?.length) lines.push(`Scoped files: ${params.files.join(', ')}`)
  if (params.context) lines.push(`Context:\n${params.context}`)
  return lines.join('\n\n')
}

export function createInvokeTeamTool(currentRole: 'director' | 'tech-lead' | 'engineer'): AnyTool {
  return {
    definition: {
      name: 'invoke_team',
      description: 'Invoke persistent team members (tech-lead or engineer).',
      input_schema: {
        type: 'object',
        properties: {
          role: { type: 'string', enum: ['tech-lead', 'engineer'] },
          task: { type: 'string' },
          domain: { type: 'string' },
          files: { type: 'array', items: { type: 'string' } },
          context: { type: 'string' },
          worktree: { type: 'boolean' },
        },
        required: ['role', 'task'],
      },
    },

    async execute(params: InvokeTeamInput, ctx: ToolContext): Promise<ToolResult> {
      if (currentRole === 'engineer') {
        return { success: false, output: 'Engineers cannot invoke team members.' }
      }
      if (currentRole === 'director' && params.role !== 'tech-lead') {
        return { success: false, output: 'Director can only invoke tech-lead.' }
      }
      if (currentRole === 'tech-lead' && params.role !== 'engineer') {
        return { success: false, output: 'Tech Lead can only invoke engineer.' }
      }

      const childId = crypto.randomUUID()
      const wantsWorktree = params.worktree ?? params.role === 'engineer'
      const allowedTools = applyTierToolPolicy(params.role, [
        'read_file',
        'write_file',
        'edit',
        'create_file',
        'glob',
        'grep',
        'bash',
        'invoke_subagent',
        'attempt_completion',
        'websearch',
        'webfetch',
        'invoke_team',
        'apply_patch',
      ])

      let cwd = ctx.workingDirectory
      let branch: string | undefined
      let worktreePath: string | undefined

      if (wantsWorktree) {
        try {
          const wt = await createWorktree(ctx.workingDirectory, childId)
          cwd = wt.path
          branch = `ava/engineer/${childId}`
          worktreePath = wt.path
        } catch {
          cwd = ctx.workingDirectory
        }
      }

      ctx.onEvent?.({
        type: params.role === 'tech-lead' ? 'praxis:lead-assigned' : 'praxis:engineer-spawned',
        agentId: ctx.sessionId,
        childAgentId: childId,
        role: params.role,
      })

      const child = new AgentExecutor(
        {
          id: childId,
          name: `${params.role}:${childId.slice(0, 8)}`,
          provider: (ctx.provider as LLMProvider | undefined) ?? 'openrouter',
          model:
            params.role === 'tech-lead'
              ? 'anthropic/claude-sonnet-4-6'
              : 'anthropic/claude-haiku-4-5',
          allowedTools,
          maxTurns: params.role === 'tech-lead' ? 14 : 12,
          maxTimeMinutes: 12,
          systemPrompt: getTierPrompt(params.role),
          delegationDepth: (ctx.delegationDepth ?? 0) + 1,
        },
        ctx.onEvent
      )

      const abort = new AbortController()
      registerExecutor(childId, child, abort, ctx.sessionId, params.role)

      try {
        const signal = AbortSignal.any([ctx.signal, abort.signal])
        const result = await child.run({ goal: buildGoal(params), cwd }, signal)
        const payload: InvokeTeamResult = {
          agentId: childId,
          success: result.success,
          summary: result.output,
          filesChanged: parseChangedFiles(result.output),
          worktreeBranch: branch,
        }

        return {
          success: result.success,
          output: result.output,
          metadata: payload as unknown as Record<string, unknown>,
        }
      } finally {
        unregisterExecutor(childId)
        if (worktreePath && !ctx.signal.aborted) {
          await removeWorktree(ctx.workingDirectory, worktreePath).catch(() => undefined)
        }
      }
    },
  }
}
