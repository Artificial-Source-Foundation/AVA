import type { ChatMessage, LLMProvider } from '@ava/core-v2/llm'
import { createClient } from '@ava/core-v2/llm'

export interface SecurityPolicy {
  allowedTools: string[]
  allowedPaths: string[]
  deniedCommands: string[]
  networkAccess: boolean
  reasoning: string
}

const DEFAULT_DENIED_COMMANDS = ['rm -rf /', 'chmod 777', 'curl *|sh', 'wget *|sh']

function extractJsonObject(raw: string): string | null {
  const start = raw.indexOf('{')
  const end = raw.lastIndexOf('}')
  if (start === -1 || end === -1 || end <= start) return null
  return raw.slice(start, end + 1)
}

function toStringArray(value: unknown): string[] {
  if (!Array.isArray(value)) return []
  return value.filter((item): item is string => typeof item === 'string' && item.length > 0)
}

function normalizePath(path: string): string {
  return path.replace(/\\/g, '/')
}

function defaultPolicy(workingDirectory: string, reason: string): SecurityPolicy {
  const normalized = normalizePath(workingDirectory)
  return {
    allowedTools: [],
    allowedPaths: [`${normalized}/**`],
    deniedCommands: DEFAULT_DENIED_COMMANDS,
    networkAccess: false,
    reasoning: reason,
  }
}

function parsePolicy(
  raw: string,
  availableTools: string[],
  workingDirectory: string
): SecurityPolicy {
  const fallback = defaultPolicy(
    workingDirectory,
    'Failed to parse generated policy. Blocking by default.'
  )
  const payload = extractJsonObject(raw)
  if (!payload) {
    return fallback
  }

  try {
    const parsed = JSON.parse(payload) as {
      allowedTools?: unknown
      allowedPaths?: unknown
      deniedCommands?: unknown
      networkAccess?: unknown
      reasoning?: unknown
    }

    const allowedTools = toStringArray(parsed.allowedTools).filter((tool) =>
      availableTools.includes(tool)
    )
    const allowedPaths = toStringArray(parsed.allowedPaths)
    const deniedCommands = toStringArray(parsed.deniedCommands)

    return {
      allowedTools,
      allowedPaths: allowedPaths.length > 0 ? allowedPaths : fallback.allowedPaths,
      deniedCommands: deniedCommands.length > 0 ? deniedCommands : DEFAULT_DENIED_COMMANDS,
      networkAccess: parsed.networkAccess === true,
      reasoning:
        typeof parsed.reasoning === 'string' && parsed.reasoning.length > 0
          ? parsed.reasoning
          : 'Generated policy did not include reasoning.',
    }
  } catch {
    return fallback
  }
}

function matchesGlob(value: string, pattern: string): boolean {
  const escaped = pattern
    .replace(/[.+^${}()|[\]\\]/g, '\\$&')
    .replace(/\*\*/g, '§§')
    .replace(/\*/g, '[^/]*')
    .replace(/§§/g, '.*')
    .replace(/\?/g, '.')
  return new RegExp(`^${escaped}$`).test(value)
}

function matchesCommandPattern(command: string, pattern: string): boolean {
  const trimmed = pattern.trim()
  if (trimmed.length === 0) return false

  if (trimmed.startsWith('/') && trimmed.endsWith('/') && trimmed.length > 2) {
    try {
      return new RegExp(trimmed.slice(1, -1)).test(command)
    } catch {
      return false
    }
  }

  if (trimmed.includes('*') || trimmed.includes('?')) {
    const escaped = trimmed
      .replace(/[.+^${}()|[\]\\]/g, '\\$&')
      .replace(/\*/g, '.*')
      .replace(/\?/g, '.')
    return new RegExp(`^${escaped}$`).test(command)
  }

  return command.includes(trimmed)
}

function getPathArg(args: Record<string, unknown>): string | undefined {
  const path = args.path ?? args.filePath ?? args.file_path
  return typeof path === 'string' ? normalizePath(path) : undefined
}

export async function generatePolicy(
  goal: string,
  availableTools: string[],
  workingDirectory: string,
  provider: LLMProvider,
  model: string
): Promise<SecurityPolicy> {
  const messages: ChatMessage[] = [
    {
      role: 'system',
      content:
        'You generate strict least-privilege tool policies. Return only JSON with keys allowedTools, allowedPaths, deniedCommands, networkAccess, reasoning.',
    },
    {
      role: 'user',
      content: [
        `Goal: ${goal}`,
        `Working directory: ${workingDirectory}`,
        `Available tools: ${availableTools.join(', ')}`,
        'Choose the minimum required tools and paths for this goal.',
      ].join('\n'),
    },
  ]

  try {
    const client = createClient(provider)
    let output = ''
    const timeoutSignal = AbortSignal.timeout(15_000)

    for await (const delta of client.stream(
      messages,
      {
        provider,
        model,
        maxTokens: 500,
        temperature: 0,
      },
      timeoutSignal
    )) {
      if (delta.error) {
        return defaultPolicy(workingDirectory, `Policy generation error: ${delta.error.message}`)
      }
      if (delta.content) {
        output += delta.content
      }
    }

    return parsePolicy(output, availableTools, workingDirectory)
  } catch (error) {
    const reason = error instanceof Error ? error.message : 'Unknown policy generation error'
    return defaultPolicy(workingDirectory, `Policy generation failed: ${reason}`)
  }
}

export function enforcePolicy(
  policy: SecurityPolicy,
  toolName: string,
  args: Record<string, unknown>
): { allowed: boolean; reason?: string } {
  if (!policy.allowedTools.includes(toolName)) {
    return { allowed: false, reason: `Tool '${toolName}' is not allowed by generated policy.` }
  }

  const path = getPathArg(args)
  if (path && policy.allowedPaths.length > 0) {
    const isPathAllowed = policy.allowedPaths.some((pattern) =>
      matchesGlob(path, normalizePath(pattern))
    )
    if (!isPathAllowed) {
      return { allowed: false, reason: `Path '${path}' is not allowed by generated policy.` }
    }
  }

  if (toolName === 'bash' && typeof args.command === 'string') {
    const blocked = policy.deniedCommands.find((pattern) =>
      matchesCommandPattern(args.command as string, pattern)
    )
    if (blocked) {
      return { allowed: false, reason: `Command blocked by generated policy pattern '${blocked}'.` }
    }
  }

  return { allowed: true }
}

export function createPolicyCache(): {
  get(sessionId: string): SecurityPolicy | undefined
  set(sessionId: string, policy: SecurityPolicy): void
  getOrCreate(sessionId: string, create: () => Promise<SecurityPolicy>): Promise<SecurityPolicy>
  clear(sessionId?: string): void
} {
  const policyBySession = new Map<string, SecurityPolicy>()
  const inflight = new Map<string, Promise<SecurityPolicy>>()

  return {
    get(sessionId: string): SecurityPolicy | undefined {
      return policyBySession.get(sessionId)
    },
    set(sessionId: string, policy: SecurityPolicy): void {
      policyBySession.set(sessionId, policy)
    },
    async getOrCreate(
      sessionId: string,
      create: () => Promise<SecurityPolicy>
    ): Promise<SecurityPolicy> {
      const existing = policyBySession.get(sessionId)
      if (existing) return existing

      const active = inflight.get(sessionId)
      if (active) return active

      const next = create()
      inflight.set(sessionId, next)

      try {
        const resolved = await next
        policyBySession.set(sessionId, resolved)
        return resolved
      } finally {
        inflight.delete(sessionId)
      }
    },
    clear(sessionId?: string): void {
      if (sessionId) {
        policyBySession.delete(sessionId)
        inflight.delete(sessionId)
        return
      }
      policyBySession.clear()
      inflight.clear()
    },
  }
}
