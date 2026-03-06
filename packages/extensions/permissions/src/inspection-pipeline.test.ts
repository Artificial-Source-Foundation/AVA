import { describe, expect, it, vi } from 'vitest'
import {
  InspectionPipeline,
  type Inspector,
  PermissionInspector,
  RepetitionInspector,
  SecurityInspector,
} from './inspection-pipeline.js'

type ToolContext = {
  sessionId: string
  workingDirectory: string
  signal: AbortSignal
}

function makeContext(overrides: Partial<ToolContext> = {}): ToolContext {
  return {
    sessionId: 'session-1',
    workingDirectory: '/workspace',
    signal: new AbortController().signal,
    ...overrides,
  }
}

describe('InspectionPipeline', () => {
  it('returns allow when all inspectors allow', async () => {
    const pipeline = new InspectionPipeline()
    pipeline.register(new SecurityInspector())
    pipeline.register(new PermissionInspector({ allowlist: ['read'] }))
    pipeline.register(new RepetitionInspector(4))

    const result = await pipeline.inspect('read', { path: '/workspace/a.txt' }, makeContext())

    expect(result).toEqual({ action: 'allow' })
  })

  it('short-circuits on security deny before permission inspector', async () => {
    const pipeline = new InspectionPipeline()
    pipeline.register(new SecurityInspector())

    const permissionInspect = vi.fn(async () => ({ action: 'allow' as const }))
    const permissionInspector: Inspector = {
      name: 'permission-spy',
      layer: 'permission',
      inspect: permissionInspect,
    }
    pipeline.register(permissionInspector)

    const result = await pipeline.inspect('bash', { command: 'rm -rf /' }, makeContext())

    expect(result.action).toBe('deny')
    expect(permissionInspect).not.toHaveBeenCalled()
  })

  it('returns escalate with reason', async () => {
    const pipeline = new InspectionPipeline()
    pipeline.register(new RepetitionInspector(2))

    const first = await pipeline.inspect('read', { path: '/workspace/a.txt' }, makeContext())
    const second = await pipeline.inspect('read', { path: '/workspace/a.txt' }, makeContext())

    expect(first).toEqual({ action: 'allow' })
    expect(second.action).toBe('escalate')
    if (second.action === 'escalate') {
      expect(second.reason).toContain('Repeated identical call')
    }
  })

  it('runs in layer order with security first', async () => {
    const calls: string[] = []

    const permissionInspector: Inspector = {
      name: 'permission',
      layer: 'permission',
      async inspect() {
        calls.push('permission')
        return { action: 'allow' }
      },
    }

    const securityInspector: Inspector = {
      name: 'security',
      layer: 'security',
      async inspect() {
        calls.push('security')
        return { action: 'allow' }
      },
    }

    const repetitionInspector: Inspector = {
      name: 'repetition',
      layer: 'repetition',
      async inspect() {
        calls.push('repetition')
        return { action: 'allow' }
      },
    }

    const pipeline = new InspectionPipeline()
    pipeline.register(permissionInspector)
    pipeline.register(repetitionInspector)
    pipeline.register(securityInspector)

    const result = await pipeline.inspect('read', { path: '/workspace/a.txt' }, makeContext())

    expect(result).toEqual({ action: 'allow' })
    expect(calls).toEqual(['security', 'permission', 'repetition'])
  })

  it('security inspector catches dangerous command patterns', async () => {
    const inspector = new SecurityInspector()

    const result = await inspector.inspect(
      'bash',
      { command: 'chmod 777 /tmp/file' },
      makeContext({ workingDirectory: '/workspace' })
    )

    expect(result.action).toBe('deny')
    if (result.action === 'deny') {
      expect(result.reason).toContain('dangerous')
    }
  })
})
