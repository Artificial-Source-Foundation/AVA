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

const SESSION_ROLE = new Map<string, 'director' | 'tech-lead' | 'engineer'>()

function inferCallerRole(ctx: ToolContext): 'director' | 'tech-lead' | 'engineer' {
  const known = SESSION_ROLE.get(ctx.sessionId)
  if (known) return known
  if ((ctx.delegationDepth ?? 0) <= 0) return 'director'
  return 'engineer'
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

export function createInvokeTeamTool(): AnyTool {
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
      const callerRole = inferCallerRole(ctx)
      if (callerRole === 'engineer') {
        return { success: false, output: 'Engineers cannot invoke team members.' }
      }
      if (callerRole === 'director' && params.role !== 'tech-lead' && params.role !== 'engineer') {
        return { success: false, output: 'Director can invoke tech-lead or engineer.' }
      }
      if (callerRole === 'tech-lead' && params.role !== 'engineer') {
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
          branch = wt.branch
          worktreePath = wt.path
        } catch {
          cwd = ctx.workingDirectory
        }
      }

      ctx.onEvent?.({
        type: params.role === 'tech-lead' ? 'praxis:lead-assigned' : 'praxis:engineer-spawned',
        agentId: ctx.sessionId,
        childAgentId: childId,
        leadId: params.role === 'engineer' ? ctx.sessionId : childId,
        task: params.task,
        domain: params.domain,
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
      SESSION_ROLE.set(childId, params.role)

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
        SESSION_ROLE.delete(childId)
        if (worktreePath && !ctx.signal.aborted) {
          await removeWorktree(ctx.workingDirectory, worktreePath).catch(() => undefined)
        }
      }
    },
  }
}
