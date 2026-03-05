import { dispatchCompute } from '@ava/core-v2'
import type { AgentResult, ToolCallInfo } from '@ava/core-v2/agent'
import type { Disposable, ExtensionAPI, ToolMiddleware } from '@ava/core-v2/extensions'

const MUTATING_TOOLS = new Set(['edit', 'write_file', 'create_file', 'apply_patch', 'multiedit'])

interface ReliabilityState {
  recentCallSignatures: string[]
  noFileChangeTurns: number
  totalTokensSpent: number
  budgetRatio: number
  stuckHits: number
  lastTurnNoTools: boolean
  modifiedFiles: Set<string>
}

function getSignature(toolName: string, args: Record<string, unknown>): string {
  return `${toolName}:${JSON.stringify(args, Object.keys(args).sort())}`
}

function isSummaryLike(text: string): boolean {
  if (text.length < 40) {
    return false
  }
  const normalized = text.toLowerCase()
  return (
    normalized.includes('summary') ||
    normalized.includes('completed') ||
    normalized.includes('implemented') ||
    normalized.includes('finished') ||
    normalized.includes('done')
  )
}

function filePathFromArgs(args: Record<string, unknown>): string | null {
  const path = args.path
  if (typeof path === 'string') return path
  const filePath = args.filePath
  if (typeof filePath === 'string') return filePath
  return null
}

async function validateModifiedFiles(api: ExtensionAPI, files: Set<string>): Promise<boolean> {
  for (const file of files) {
    try {
      const content = await api.platform.fs.readFile(file)
      const result = await dispatchCompute<{ valid: boolean }>(
        'validation_validate_edit',
        { content },
        async () => ({ valid: true })
      )
      if (!result.valid) {
        return false
      }
    } catch {
      return false
    }
  }
  return true
}

export function createReliabilityMiddleware(api: ExtensionAPI): {
  middleware: ToolMiddleware
  dispose: Disposable
} {
  const states = new Map<string, ReliabilityState>()
  const agentSettings = api.getSettings('agent') as { contextWindowTokens?: number } | undefined
  const contextWindow = Number(agentSettings?.contextWindowTokens ?? 200_000)

  const getState = (sessionId: string): ReliabilityState => {
    const existing = states.get(sessionId)
    if (existing) return existing
    const fresh: ReliabilityState = {
      recentCallSignatures: [],
      noFileChangeTurns: 0,
      totalTokensSpent: 0,
      budgetRatio: 0,
      stuckHits: 0,
      lastTurnNoTools: false,
      modifiedFiles: new Set<string>(),
    }
    states.set(sessionId, fresh)
    return fresh
  }

  const turnDisposable = api.on('turn:end', (payload: unknown) => {
    const event = payload as {
      agentId?: string
      toolCalls?: ToolCallInfo[]
    }
    if (!event.agentId) return
    const state = getState(event.agentId)
    const toolCalls = event.toolCalls ?? []
    state.lastTurnNoTools = toolCalls.length === 0

    const hasFileMutation = toolCalls.some((call) => MUTATING_TOOLS.has(call.name) && call.success)
    if (hasFileMutation) {
      state.noFileChangeTurns = 0
    } else {
      state.noFileChangeTurns += 1
    }

    for (const call of toolCalls) {
      if (!call.success || !MUTATING_TOOLS.has(call.name)) continue
      const file = filePathFromArgs(call.args)
      if (file) state.modifiedFiles.add(file)
    }
  })

  const usageDisposable = api.on('llm:usage', (payload: unknown) => {
    const event = payload as {
      sessionId?: string
      inputTokens?: number
      outputTokens?: number
    }
    if (!event.sessionId) return
    const state = getState(event.sessionId)
    const spent = (event.inputTokens ?? 0) + (event.outputTokens ?? 0)
    state.totalTokensSpent += spent
    state.budgetRatio = state.totalTokensSpent / contextWindow
  })

  const finishDisposable = api.on('agent:finish', (payload: unknown) => {
    const event = payload as { agentId?: string; result?: AgentResult }
    if (!event.agentId || !event.result) return
    const state = getState(event.agentId)
    const output = event.result.output ?? ''

    if (!state.lastTurnNoTools || !isSummaryLike(output) || state.modifiedFiles.size === 0) {
      return
    }

    void validateModifiedFiles(api, state.modifiedFiles)
      .then((valid) => {
        if (valid) {
          api.emit('agent:auto-completed', {
            agentId: event.agentId,
            filesValidated: state.modifiedFiles.size,
            timestamp: Date.now(),
          })
        } else {
          api.emit('stuck:detected', {
            sessionId: event.agentId,
            scenario: 'completion-validation-failed',
            severity: 'high',
            recommendation:
              'Final response looked complete but file validation failed. Ask user before finishing.',
            timestamp: Date.now(),
          })
        }
      })
      .catch((error) => {
        api.log.warn('Completion validation failed unexpectedly', {
          sessionId: event.agentId,
          error: error instanceof Error ? error.message : String(error),
        })
      })
  })

  const middleware: ToolMiddleware = {
    name: 'reliability-stuck-detection',
    priority: 5,
    async before(ctx) {
      const state = getState(ctx.ctx.sessionId)

      if (ctx.toolName === 'attempt_completion') {
        const completionText =
          (typeof ctx.args.result === 'string' ? ctx.args.result : null) ??
          (typeof ctx.args.output === 'string' ? ctx.args.output : null) ??
          ''

        if (state.modifiedFiles.size > 0 && isSummaryLike(completionText)) {
          const valid = await validateModifiedFiles(api, state.modifiedFiles)
          if (!valid) {
            api.emit('stuck:detected', {
              sessionId: ctx.ctx.sessionId,
              scenario: 'completion-validation-failed',
              severity: 'high',
              recommendation: 'Edited files failed validation. Ask user before final completion.',
              timestamp: Date.now(),
            })
            return {
              blocked: true,
              reason:
                'Completion blocked: modified files failed validation. Ask user for guidance before finishing.',
            }
          }
          api.emit('agent:auto-completed', {
            agentId: ctx.ctx.sessionId,
            filesValidated: state.modifiedFiles.size,
            timestamp: Date.now(),
          })
        }
      }

      const signature = getSignature(ctx.toolName, ctx.args)
      state.recentCallSignatures.push(signature)
      if (state.recentCallSignatures.length > 5) {
        state.recentCallSignatures.shift()
      }

      const repeated = state.recentCallSignatures.filter((item) => item === signature).length >= 2
      const spinning = state.noFileChangeTurns >= 5
      const nearBudget = state.budgetRatio >= 0.9

      if (!repeated && !spinning && !nearBudget) {
        return undefined
      }

      state.stuckHits += 1
      api.emit('agent:follow-up-queued', {
        agentId: ctx.ctx.sessionId,
        message:
          'Possible loop detected. Try a different strategy, reduce scope, or ask the user for clarification.',
      })

      if (state.stuckHits >= 2) {
        api.emit('stuck:detected', {
          sessionId: ctx.ctx.sessionId,
          scenario: repeated ? 'repeat-2-of-5' : spinning ? 'no-file-progress' : 'budget-over-90',
          severity: 'high',
          recommendation:
            'Escalate to user for guidance before continuing. Tool execution blocked to prevent loop.',
          timestamp: Date.now(),
        })
        return {
          blocked: true,
          reason:
            'Potential loop detected. Ask the user for guidance before continuing with more tool calls.',
        }
      }

      return undefined
    },
  }

  return {
    middleware,
    dispose: {
      dispose() {
        turnDisposable.dispose()
        usageDisposable.dispose()
        finishDisposable.dispose()
      },
    },
  }
}
