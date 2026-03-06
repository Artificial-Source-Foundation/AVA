import type { ToolContext } from '@ava/core-v2'

export type InspectionResult =
  | { action: 'allow' }
  | { action: 'deny'; reason: string }
  | { action: 'escalate'; reason: string }

export interface Inspector {
  name: string
  layer: 'security' | 'permission' | 'repetition'
  inspect(
    toolName: string,
    args: Record<string, unknown>,
    context: ToolContext
  ): Promise<InspectionResult>
}

const LAYER_ORDER: ReadonlyArray<Inspector['layer']> = ['security', 'permission', 'repetition']

function stableStringify(value: unknown): string {
  if (value === null || typeof value !== 'object') {
    return JSON.stringify(value)
  }

  if (Array.isArray(value)) {
    return `[${value.map((item) => stableStringify(item)).join(',')}]`
  }

  const record = value as Record<string, unknown>
  const keys = Object.keys(record).sort((a, b) => a.localeCompare(b))
  return `{${keys.map((key) => `${JSON.stringify(key)}:${stableStringify(record[key])}`).join(',')}}`
}

function getPathArg(args: Record<string, unknown>): string | undefined {
  const value = args.path ?? args.filePath ?? args.file_path
  return typeof value === 'string' ? value : undefined
}

export class InspectionPipeline {
  private inspectors: Inspector[] = []

  register(inspector: Inspector): void {
    this.inspectors.push(inspector)
  }

  async inspect(
    toolName: string,
    args: Record<string, unknown>,
    context: ToolContext
  ): Promise<InspectionResult> {
    const ordered = [...this.inspectors].sort(
      (a, b) => LAYER_ORDER.indexOf(a.layer) - LAYER_ORDER.indexOf(b.layer)
    )

    for (const inspector of ordered) {
      const result = await inspector.inspect(toolName, args, context)
      if (result.action !== 'allow') {
        return result
      }
    }

    return { action: 'allow' }
  }
}

export class SecurityInspector implements Inspector {
  readonly name = 'security-inspector'
  readonly layer = 'security' as const

  async inspect(
    toolName: string,
    args: Record<string, unknown>,
    context: ToolContext
  ): Promise<InspectionResult> {
    if (toolName === 'bash') {
      const command = typeof args.command === 'string' ? args.command : ''
      const dangerousPatterns = [
        /rm\s+-rf\s+\//,
        /chmod\s+777\b/,
        /(curl|wget).*(\||&&)\s*(sh|bash)/i,
      ]
      if (dangerousPatterns.some((pattern) => pattern.test(command))) {
        return { action: 'deny', reason: `Blocked dangerous command: ${command}` }
      }
      if (/\$\(/.test(command) && /(export\s+\w+=|env\s+\w+=)/.test(command)) {
        return { action: 'deny', reason: 'Potential environment variable injection detected.' }
      }
    }

    const filePath = getPathArg(args)
    if (filePath && filePath.includes('..')) {
      return { action: 'deny', reason: `Path traversal detected: ${filePath}` }
    }

    if (context.workingDirectory === '/' && (toolName === 'delete_file' || toolName === 'bash')) {
      return {
        action: 'escalate',
        reason: 'Destructive operation in root directory requires approval.',
      }
    }

    return { action: 'allow' }
  }
}

export class PermissionInspector implements Inspector {
  readonly name = 'permission-inspector'
  readonly layer = 'permission' as const

  constructor(
    private readonly config: {
      allowlist?: string[]
      denylist?: string[]
    }
  ) {}

  async inspect(
    toolName: string,
    _args: Record<string, unknown>,
    _context: ToolContext
  ): Promise<InspectionResult> {
    if (this.config.denylist?.includes(toolName)) {
      return { action: 'deny', reason: `Tool '${toolName}' denied by permission policy.` }
    }

    if ((this.config.allowlist?.length ?? 0) > 0 && !this.config.allowlist?.includes(toolName)) {
      return { action: 'deny', reason: `Tool '${toolName}' not present in permission allowlist.` }
    }

    return { action: 'allow' }
  }
}

export class RepetitionInspector implements Inspector {
  readonly name = 'repetition-inspector'
  readonly layer = 'repetition' as const

  private signaturesBySession = new Map<string, { signature: string; count: number }>()

  constructor(private readonly threshold = 3) {}

  async inspect(
    toolName: string,
    args: Record<string, unknown>,
    context: ToolContext
  ): Promise<InspectionResult> {
    const sessionKey = context.sessionId
    const signature = `${toolName}:${stableStringify(args)}`
    const current = this.signaturesBySession.get(sessionKey)

    if (!current || current.signature !== signature) {
      this.signaturesBySession.set(sessionKey, { signature, count: 1 })
      return { action: 'allow' }
    }

    const nextCount = current.count + 1
    this.signaturesBySession.set(sessionKey, { signature, count: nextCount })

    if (nextCount >= this.threshold) {
      return {
        action: 'escalate',
        reason: `Repeated identical call detected ${nextCount} times for ${toolName}.`,
      }
    }

    return { action: 'allow' }
  }
}
