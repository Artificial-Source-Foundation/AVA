import { extractCommandPrefix } from './arity.js'
import { parseBashTokens } from './bash-parser.js'

export function buildApprovalKey(toolName: string, args: Record<string, unknown>): string {
  if (toolName !== 'bash') {
    return toolName
  }

  const command = typeof args.command === 'string' ? args.command : ''
  if (!command) {
    return toolName
  }

  const tokens = parseBashTokens(command)
  const prefix = extractCommandPrefix(tokens)
  if (prefix.length === 0) {
    return toolName
  }
  return `${toolName}:${prefix.join(':')}`
}

function isDangerousToGeneralize(command: string): boolean {
  const trimmed = command.trim()
  return (
    /rm\s+-rf\b/.test(trimmed) ||
    /(?:^|\s)(mkfs|dd|shutdown|reboot)(?:\s|$)/.test(trimmed) ||
    /chmod\s+777\b/.test(trimmed) ||
    /curl\s+[^|]*\|\s*(bash|sh)\b/.test(trimmed)
  )
}

function learnedKeys(toolName: string, args: Record<string, unknown>): string[] {
  const exact = buildApprovalKey(toolName, args)
  if (toolName !== 'bash') {
    return [exact]
  }

  const command = typeof args.command === 'string' ? args.command.trim() : ''
  if (!command || isDangerousToGeneralize(command)) {
    return [exact]
  }

  const prefix = extractCommandPrefix(parseBashTokens(command))
  if (prefix.length < 2) {
    return [exact]
  }

  if (prefix[0] === 'git') {
    // Intentionally conservative session generalization: only read-only git commands
    // are widened to `git *`. Other commands remain exact-match approvals.
    const safeSubcommands = new Set(['status', 'log', 'diff', 'show'])
    if (safeSubcommands.has(prefix[1] ?? '')) {
      return ['bash:git:*', exact]
    }
  }

  return [exact]
}

export interface DynamicRuleStore {
  startSession(sessionId: string): void
  allows(sessionId: string, toolName: string, args: Record<string, unknown>): boolean
  learn(sessionId: string, toolName: string, args: Record<string, unknown>): void
  clear(): void
}

export function createDynamicRuleStore(): DynamicRuleStore {
  let activeSessionId: string | null = null
  const rules = new Set<string>()

  const startSession = (sessionId: string): void => {
    if (activeSessionId === sessionId) {
      return
    }
    activeSessionId = sessionId
    rules.clear()
  }

  const allows = (sessionId: string, toolName: string, args: Record<string, unknown>): boolean => {
    startSession(sessionId)

    const exact = buildApprovalKey(toolName, args)
    if (rules.has(exact)) {
      return true
    }

    if (toolName === 'bash') {
      const parts = exact.split(':')
      if (parts.length >= 2) {
        const wildcard = `${parts[0]}:${parts[1]}:*`
        if (rules.has(wildcard)) {
          return true
        }
      }
    }

    return false
  }

  const learn = (sessionId: string, toolName: string, args: Record<string, unknown>): void => {
    startSession(sessionId)
    for (const key of learnedKeys(toolName, args)) {
      rules.add(key)
    }
  }

  const clear = (): void => {
    activeSessionId = null
    rules.clear()
  }

  return { startSession, allows, learn, clear }
}

export { isDangerousToGeneralize }
