import type {
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'

const ALWAYS_SANDBOX = [
  /(?:^|\s)npm\s+(?:install|i)(?:\s|$)/,
  /(?:^|\s)pnpm\s+(?:install|add)(?:\s|$)/,
  /(?:^|\s)yarn\s+(?:install|add)(?:\s|$)/,
  /(?:^|\s)pip\s+install(?:\s|$)/,
  /(?:^|\s)cargo\s+install(?:\s|$)/,
]

const NEVER_SANDBOX = [/^(?:\s*git\s+status\b)/, /^(?:\s*ls\b)/, /^(?:\s*cat\b)/]

function shouldSandbox(command: string): boolean {
  const trimmed = command.trim()
  if (NEVER_SANDBOX.some((pattern) => pattern.test(trimmed))) return false
  return ALWAYS_SANDBOX.some((pattern) => pattern.test(trimmed))
}

export function createSandboxMiddleware(): ToolMiddleware {
  return {
    name: 'ava-sandbox',
    priority: 3,
    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      if (ctx.toolName !== 'bash') return undefined
      const command = typeof ctx.args.command === 'string' ? ctx.args.command : ''
      const sandboxed = command ? shouldSandbox(command) : false

      return {
        args: {
          ...ctx.args,
          _sandboxed: sandboxed,
          _sandboxPolicy: {
            writableRoots: [ctx.ctx.workingDirectory, '/tmp'],
            networkAccess: false,
          },
        },
      }
    },
  }
}
