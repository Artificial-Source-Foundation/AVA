import { MessageBus } from '@ava/core-v2/bus'
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

// ─── Bus-Based Approval ──────────────────────────────────────────────────

describe('Bus-based approval', () => {
  afterEach(() => {
    resetSettings()
  })

  it('requests approval via bus when subscriber exists', async () => {
    const bus = new MessageBus()
    const mw = createPermissionMiddleware(bus)

    // Subscriber auto-approves
    bus.subscribe('permission:request', (msg) => {
      bus.publish({
        type: 'permission:response',
        correlationId: msg.correlationId,
        timestamp: Date.now(),
        approved: true,
      })
    })

    const result = await mw.before!(makeCtx('write_file', { path: '/project/file.ts' }))
    expect(result).toBeUndefined()
  })

  it('blocks when user denies via bus', async () => {
    const bus = new MessageBus()
    const mw = createPermissionMiddleware(bus)

    bus.subscribe('permission:request', (msg) => {
      bus.publish({
        type: 'permission:response',
        correlationId: msg.correlationId,
        timestamp: Date.now(),
        approved: false,
        reason: 'Not today',
      })
    })

    const result = await mw.before!(makeCtx('write_file', { path: '/project/file.ts' }))
    expect(result?.blocked).toBe(true)
    expect(result?.reason).toBe('Not today')
  })

  it('falls back to default behavior when no bus subscribers', async () => {
    const bus = new MessageBus()
    const mw = createPermissionMiddleware(bus)
    // No subscribers — should use fallback (auto-approve for reads)

    const result = await mw.before!(makeCtx('read_file', { path: '/project/file.ts' }))
    expect(result).toBeUndefined()
  })

  it('still auto-approves reads even with bus', async () => {
    const bus = new MessageBus()
    const mw = createPermissionMiddleware(bus)

    // Subscriber that would deny (shouldn't be called for reads)
    let called = false
    bus.subscribe('permission:request', (msg) => {
      called = true
      bus.publish({
        type: 'permission:response',
        correlationId: msg.correlationId,
        timestamp: Date.now(),
        approved: false,
      })
    })

    const result = await mw.before!(makeCtx('read_file', { path: '/project/file.ts' }))
    expect(result).toBeUndefined()
    expect(called).toBe(false)
  })

  it('skips bus approval in yolo mode', async () => {
    const bus = new MessageBus()
    const mw = createPermissionMiddleware(bus)
    updateSettings({ yolo: true })

    let called = false
    bus.subscribe('permission:request', () => {
      called = true
    })

    const result = await mw.before!(makeCtx('write_file', { path: '/project/file.ts' }))
    expect(result).toBeUndefined()
    expect(called).toBe(false)
  })
})
