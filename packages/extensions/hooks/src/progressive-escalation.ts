import type { ExtensionAPI, ToolMiddleware } from '@ava/core-v2/extensions'

export interface EscalationLevel {
  level: number
  message: string
  forceStrategy?: string
  compressContext?: boolean
}

const ESCALATION_LEVELS: EscalationLevel[] = [
  {
    level: 1,
    message: 'The previous action failed: {error}. Please retry with a different approach.',
  },
  {
    level: 2,
    message:
      'Second consecutive failure. The current approach is not working.\n{error}\nTry a fundamentally different strategy (e.g., write_file instead of edit, or break into smaller changes).',
    forceStrategy: 'write_file',
  },
  {
    level: 3,
    message:
      'Multiple consecutive failures detected. This may indicate a deeper issue.\n{error}\nStop and re-read the target file before attempting further changes. Consider whether the file structure has changed.',
    compressContext: true,
  },
]

function resolveEscalationLevel(consecutiveFailures: number): EscalationLevel {
  if (consecutiveFailures >= 3) {
    return ESCALATION_LEVELS[2] as EscalationLevel
  }
  if (consecutiveFailures === 2) {
    return ESCALATION_LEVELS[1] as EscalationLevel
  }
  return ESCALATION_LEVELS[0] as EscalationLevel
}

function sessionKey(sessionId: string | undefined): string {
  return sessionId ?? 'global'
}

export function createProgressiveEscalationMiddleware(
  api: ExtensionAPI,
  logger: ExtensionAPI['log']
): ToolMiddleware {
  const consecutiveFailures = new Map<string, number>()

  return {
    name: 'progressive-escalation',
    priority: 6,
    async after(ctx, result) {
      const key = sessionKey(ctx.ctx.sessionId)

      if (result.success) {
        if ((consecutiveFailures.get(key) ?? 0) > 0) {
          logger.debug(`Resetting failure counter for session ${key}`)
        }
        consecutiveFailures.set(key, 0)
        return undefined
      }

      const failures = (consecutiveFailures.get(key) ?? 0) + 1
      consecutiveFailures.set(key, failures)
      const level = resolveEscalationLevel(failures)
      const errorMessage = result.error ?? result.output ?? 'Unknown tool error'
      const directive = level.message.replace('{error}', errorMessage)

      const metadata = {
        ...result.metadata,
        escalation: {
          consecutiveFailures: failures,
          level: level.level,
          forceStrategy: level.forceStrategy,
          compressContext: level.compressContext === true,
        },
      }

      if (level.compressContext) {
        api.emit('escalation:max-reached', {
          sessionId: ctx.ctx.sessionId,
          toolName: ctx.toolName,
          consecutiveFailures: failures,
        })
        api.emit('context:compacting', {
          sessionId: ctx.ctx.sessionId,
          reason: 'progressive-escalation',
          consecutiveFailures: failures,
        })
      }

      if (failures >= 5) {
        api.emit('stuck:detected', {
          sessionId: ctx.ctx.sessionId,
          scenario: 'error-escalation',
          count: failures,
          suggestion:
            'Multiple failures detected. Pause, re-read target files, and switch to a simpler incremental strategy.',
          severity: 'high',
        })
      }

      return {
        result: {
          ...result,
          output: `${result.output}\n\n[Escalation L${level.level}] ${directive}`,
          metadata,
        },
      }
    },
  }
}

export { ESCALATION_LEVELS }
