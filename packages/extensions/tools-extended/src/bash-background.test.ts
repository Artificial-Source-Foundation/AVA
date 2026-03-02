/**
 * bash_background tool tests.
 */

import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { bashBackgroundTool } from './bash-background.js'
import { _resetRegistry, getProcess, listProcesses } from './process-registry.js'

function makeCtx(overrides?: Partial<{ workingDirectory: string; signal: AbortSignal }>) {
  return {
    sessionId: 'test',
    workingDirectory: overrides?.workingDirectory ?? '/tmp',
    signal: overrides?.signal ?? AbortSignal.timeout(10000),
  }
}

beforeEach(() => {
  _resetRegistry()
})

afterEach(() => {
  // Kill any spawned processes
  for (const proc of listProcesses()) {
    try {
      proc.process.kill()
    } catch {
      // already dead
    }
  }
  _resetRegistry()
})

describe('bashBackgroundTool', () => {
  it('has correct name', () => {
    expect(bashBackgroundTool.definition.name).toBe('bash_background')
  })

  it('has execute permission', () => {
    expect(bashBackgroundTool.permissions).toContain('execute')
  })

  it('spawns a background process and returns PID', async () => {
    const result = await bashBackgroundTool.execute({ command: 'sleep 60' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toMatch(/Process started with PID \d+/)
    expect(result.metadata?.pid).toBeTypeOf('number')

    const pid = result.metadata?.pid as number
    const proc = getProcess(pid)
    expect(proc).toBeDefined()
    expect(proc?.command).toBe('sleep 60')
    expect(proc?.exitCode).toBeNull()
  })

  it('registers process in the registry', async () => {
    const result = await bashBackgroundTool.execute({ command: 'sleep 60' }, makeCtx())
    expect(result.success).toBe(true)
    expect(listProcesses()).toHaveLength(1)
  })

  it('captures stdout into buffer', async () => {
    const result = await bashBackgroundTool.execute({ command: 'echo "hello world"' }, makeCtx())
    expect(result.success).toBe(true)
    const pid = result.metadata?.pid as number
    const proc = getProcess(pid)
    expect(proc).toBeDefined()

    // Wait for stdio streams to close (close fires after exit + stream drain)
    await new Promise<void>((resolve) => {
      proc!.process.on('close', () => resolve())
    })

    expect(proc!.stdout.some((line) => line.includes('hello world'))).toBe(true)
  })

  it('captures stderr into buffer', async () => {
    const result = await bashBackgroundTool.execute({ command: 'echo "err msg" >&2' }, makeCtx())
    expect(result.success).toBe(true)
    const pid = result.metadata?.pid as number
    const proc = getProcess(pid)
    expect(proc).toBeDefined()

    await new Promise<void>((resolve) => {
      proc!.process.on('close', () => resolve())
    })

    expect(proc!.stderr.some((line) => line.includes('err msg'))).toBe(true)
  })

  it('records exit code when process finishes', async () => {
    const result = await bashBackgroundTool.execute({ command: 'exit 42' }, makeCtx())
    expect(result.success).toBe(true)
    const pid = result.metadata?.pid as number
    const proc = getProcess(pid)

    await new Promise<void>((resolve) => {
      proc!.process.on('close', () => resolve())
    })

    expect(proc!.exitCode).toBe(42)
  })

  it('uses custom cwd when provided', async () => {
    const result = await bashBackgroundTool.execute({ command: 'pwd', cwd: '/home' }, makeCtx())
    expect(result.success).toBe(true)
    const pid = result.metadata?.pid as number
    const proc = getProcess(pid)

    await new Promise<void>((resolve) => {
      proc!.process.on('close', () => resolve())
    })

    expect(proc!.stdout.some((line) => line.includes('/home'))).toBe(true)
  })

  it('returns error when aborted', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await bashBackgroundTool.execute(
      { command: 'sleep 60' },
      makeCtx({ signal: controller.signal })
    )
    expect(result.success).toBe(false)
    expect(result.error).toBe('Aborted')
  })

  it('sets startTime on registered process', async () => {
    const before = Date.now()
    const result = await bashBackgroundTool.execute({ command: 'sleep 60' }, makeCtx())
    const after = Date.now()

    const pid = result.metadata?.pid as number
    const proc = getProcess(pid)
    expect(proc!.startTime).toBeGreaterThanOrEqual(before)
    expect(proc!.startTime).toBeLessThanOrEqual(after)
  })
})
