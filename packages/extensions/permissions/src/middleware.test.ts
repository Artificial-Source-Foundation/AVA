import type { ToolMiddlewareContext } from '@ava/core-v2/extensions'
import { afterEach, describe, expect, it } from 'vitest'
import { createPermissionMiddleware, resetSettings, updateSettings } from './middleware.js'

function makeCtx(toolName: string, args: Record<string, unknown> = {}): ToolMiddlewareContext {
  return {
    toolName,
    args,
    ctx: {
      sessionId: 'test',
      workingDirectory: '/tmp',
      signal: new AbortController().signal,
    },
    definition: {
      name: toolName,
      description: '',
      input_schema: { type: 'object', properties: {} },
    },
  }
}

describe('Permission Middleware', () => {
  const mw = createPermissionMiddleware()

  afterEach(() => {
    resetSettings()
  })

  // ─── Safety Blocks ──────────────────────────────────────────────────────

  it('blocks .git directory writes', async () => {
    const result = await mw.before!(makeCtx('write_file', { path: '/project/.git/config' }))
    expect(result?.blocked).toBe(true)
    expect(result?.reason).toContain('.git')
  })

  it('allows .git directory reads', async () => {
    const result = await mw.before!(makeCtx('read_file', { path: '/project/.git/config' }))
    expect(result).toBeUndefined()
  })

  it('blocks node_modules writes', async () => {
    const result = await mw.before!(
      makeCtx('write_file', { path: '/project/node_modules/foo/index.js' })
    )
    expect(result?.blocked).toBe(true)
    expect(result?.reason).toContain('node_modules')
  })

  it('allows node_modules reads', async () => {
    const result = await mw.before!(
      makeCtx('read_file', { path: '/project/node_modules/foo/index.js' })
    )
    expect(result).toBeUndefined()
  })

  it('blocks rm -rf /', async () => {
    const result = await mw.before!(makeCtx('bash', { command: 'rm -rf /' }))
    expect(result?.blocked).toBe(true)
  })

  it('blocks rm -rf ~', async () => {
    const result = await mw.before!(makeCtx('bash', { command: 'rm -rf ~' }))
    expect(result?.blocked).toBe(true)
  })

  // ─── Auto-Approve ──────────────────────────────────────────────────────

  it('auto-approves reads by default', async () => {
    const result = await mw.before!(makeCtx('read_file', { path: '/project/src/file.ts' }))
    expect(result).toBeUndefined()
  })

  it('auto-approves glob by default', async () => {
    const result = await mw.before!(makeCtx('glob', { pattern: '*.ts' }))
    expect(result).toBeUndefined()
  })

  it('auto-approves grep by default', async () => {
    const result = await mw.before!(makeCtx('grep', { pattern: 'foo' }))
    expect(result).toBeUndefined()
  })

  // ─── YOLO Mode ─────────────────────────────────────────────────────────

  it('approves writes in yolo mode', async () => {
    updateSettings({ yolo: true })
    const result = await mw.before!(makeCtx('write_file', { path: '/project/file.ts' }))
    expect(result).toBeUndefined()
  })

  it('still blocks .git in yolo mode', async () => {
    updateSettings({ yolo: true })
    const result = await mw.before!(makeCtx('write_file', { path: '/project/.git/config' }))
    expect(result?.blocked).toBe(true)
  })

  it('still blocks rm -rf in yolo mode', async () => {
    updateSettings({ yolo: true })
    const result = await mw.before!(makeCtx('bash', { command: 'rm -rf /' }))
    expect(result?.blocked).toBe(true)
  })

  // ─── Blocked Patterns ──────────────────────────────────────────────────

  it('blocks paths matching blocked patterns', async () => {
    updateSettings({ blockedPatterns: ['.env'] })
    const result = await mw.before!(makeCtx('read_file', { path: '/project/.env' }))
    expect(result?.blocked).toBe(true)
  })

  // ─── .env Warning ─────────────────────────────────────────────────────

  it('blocks .env file access when not in yolo mode', async () => {
    const result = await mw.before!(makeCtx('write_file', { path: '/project/.env.local' }))
    expect(result?.blocked).toBe(true)
    expect(result?.reason).toContain('.env')
  })

  // ─── sudo Warning ─────────────────────────────────────────────────────

  it('blocks sudo commands', async () => {
    const result = await mw.before!(makeCtx('bash', { command: 'sudo apt install foo' }))
    expect(result?.blocked).toBe(true)
    expect(result?.reason).toContain('sudo')
  })
})
