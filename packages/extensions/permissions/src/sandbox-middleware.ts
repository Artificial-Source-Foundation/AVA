import { dispatchCompute } from '@ava/core-v2'
import type {
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import { createLogger } from '@ava/core-v2/logger'

const log = createLogger('permissions:sandbox')

const ENV_DENYLIST = new Set([
  'PATH',
  'LD_PRELOAD',
  'LD_LIBRARY_PATH',
  'DYLD_INSERT_LIBRARIES',
  'DYLD_LIBRARY_PATH',
  'HOME',
  'USER',
  'SHELL',
  'EDITOR',
  'NODE_OPTIONS',
  'NODE_PATH',
  'PYTHONPATH',
  'PYTHONSTARTUP',
  'RUBYOPT',
  'PERL5OPT',
  'JAVA_TOOL_OPTIONS',
  'AWS_ACCESS_KEY_ID',
  'AWS_SECRET_ACCESS_KEY',
  'AWS_SESSION_TOKEN',
  'GOOGLE_APPLICATION_CREDENTIALS',
  'AZURE_CLIENT_SECRET',
  'DATABASE_URL',
  'REDIS_URL',
  'MONGODB_URI',
  'GH_TOKEN',
  'GITHUB_TOKEN',
  'GITLAB_TOKEN',
  'NPM_TOKEN',
  'OPENAI_API_KEY',
  'ANTHROPIC_API_KEY',
  'AVA_OPENROUTER_API_KEY',
])

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

function filterCommandEnv(rawEnv: unknown): Record<string, unknown> | undefined {
  if (!rawEnv || typeof rawEnv !== 'object') return undefined

  const env = rawEnv as Record<string, unknown>
  const filtered: Record<string, unknown> = {}
  const blocked: string[] = []

  for (const [key, value] of Object.entries(env)) {
    if (ENV_DENYLIST.has(key)) {
      blocked.push(key)
      continue
    }
    filtered[key] = value
  }

  if (blocked.length > 0) {
    log.warn('Blocked env vars for sandboxed command', {
      count: blocked.length,
      names: blocked.join(','),
    })
  }

  return Object.keys(filtered).length > 0 ? filtered : undefined
}

export function createSandboxMiddleware(): ToolMiddleware {
  return {
    name: 'ava-sandbox',
    priority: 3,
    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      if (ctx.toolName !== 'bash') return undefined
      const command = typeof ctx.args.command === 'string' ? ctx.args.command : ''
      const sandboxed = command ? shouldSandbox(command) : false
      const policy = {
        writableRoots: [ctx.ctx.workingDirectory, '/tmp'],
        networkAccess: false,
      }

      if (sandboxed && process.platform === 'linux') {
        await dispatchCompute(
          'sandbox_apply_landlock',
          {
            writableRoots: policy.writableRoots,
            network: policy.networkAccess,
          },
          async () => null
        ).catch(() => null)
      }

      const filteredEnv = sandboxed ? filterCommandEnv(ctx.args.env) : undefined

      return {
        args: {
          ...ctx.args,
          ...(filteredEnv ? { env: filteredEnv } : {}),
          _sandboxed: sandboxed,
          _sandboxPolicy: policy,
        },
      }
    },
  }
}
