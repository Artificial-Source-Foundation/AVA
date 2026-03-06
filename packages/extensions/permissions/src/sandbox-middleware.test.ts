import type { ToolMiddlewareContext } from '@ava/core-v2/extensions'
import { describe, expect, it } from 'vitest'
import { createSandboxMiddleware } from './sandbox-middleware.js'

function makeCtx(command: string): ToolMiddlewareContext {
  return {
    toolName: 'bash',
    args: { command, description: 'test' },
    ctx: {
      sessionId: 's1',
      workingDirectory: '/workspace',
      signal: new AbortController().signal,
    },
    definition: {
      name: 'bash',
      description: 'Execute shell command',
      input_schema: { type: 'object', properties: {} },
    },
  }
}

describe('createSandboxMiddleware', () => {
  it('sets priority 3', () => {
    expect(createSandboxMiddleware().priority).toBe(3)
  })

  it('sandboxes package install commands', async () => {
    const middleware = createSandboxMiddleware()
    const result = await middleware.before?.(makeCtx('npm install vite'))
    expect((result?.args as { _sandboxed?: boolean })._sandboxed).toBe(true)
  })

  it('does not sandbox git status/ls/cat commands', async () => {
    const middleware = createSandboxMiddleware()
    const gitStatus = await middleware.before?.(makeCtx('git status'))
    const ls = await middleware.before?.(makeCtx('ls -la'))
    const cat = await middleware.before?.(makeCtx('cat package.json'))

    expect((gitStatus?.args as { _sandboxed?: boolean })._sandboxed).toBe(false)
    expect((ls?.args as { _sandboxed?: boolean })._sandboxed).toBe(false)
    expect((cat?.args as { _sandboxed?: boolean })._sandboxed).toBe(false)
  })

  it('removes denied env vars before sandbox execution', async () => {
    const middleware = createSandboxMiddleware()
    const result = await middleware.before?.({
      ...makeCtx('npm install vite'),
      args: {
        command: 'npm install vite',
        description: 'test',
        env: {
          PATH: '/tmp/fake',
          OPENAI_API_KEY: 'secret',
          SAFE_VAR: 'ok',
        },
      },
    })

    const env = (result?.args as { env?: Record<string, string> })?.env ?? {}
    expect(env.PATH).toBeUndefined()
    expect(env.OPENAI_API_KEY).toBeUndefined()
    expect(env.SAFE_VAR).toBe('ok')
  })
})
