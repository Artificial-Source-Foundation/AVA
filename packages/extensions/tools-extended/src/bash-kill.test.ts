/**
 * bash_kill tool tests.
 */

import { EventEmitter } from 'node:events'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { bashKillTool } from './bash-kill.js'
import type { BackgroundProcess } from './process-registry.js'
import { _resetRegistry, getProcess, registerProcess } from './process-registry.js'

function makeFakeProcess(
  pid: number,
  opts?: Partial<{ command: string; killBehavior: 'ok' | 'throw' }>
): BackgroundProcess {
  const emitter = new EventEmitter()
  const killFn =
    opts?.killBehavior === 'throw'
      ? vi.fn(() => {
          throw new Error('kill failed')
        })
      : vi.fn()

  return {
    pid,
    command: opts?.command ?? 'test-cmd',
    stdout: [],
    stderr: [],
    startTime: Date.now(),
    exitCode: null,
    process: Object.assign(emitter, {
      pid,
      kill: killFn,
      stdin: null,
      stdout: null,
      stderr: null,
      stdio: [null, null, null] as const,
      connected: false,
      exitCode: null,
      signalCode: null,
      killed: false,
      spawnargs: [],
      spawnfile: '',
      ref: vi.fn(),
      unref: vi.fn(),
      disconnect: vi.fn(),
      send: vi.fn(),
      [Symbol.dispose]: vi.fn(),
    }) as unknown as BackgroundProcess['process'],
  }
}

function makeCtx(overrides?: Partial<{ signal: AbortSignal }>) {
  return {
    sessionId: 'test',
    workingDirectory: '/tmp',
    signal: overrides?.signal ?? AbortSignal.timeout(5000),
  }
}

beforeEach(() => {
  _resetRegistry()
})

afterEach(() => {
  _resetRegistry()
})

describe('bashKillTool', () => {
  it('has correct name', () => {
    expect(bashKillTool.definition.name).toBe('bash_kill')
  })

  it('has execute permission', () => {
    expect(bashKillTool.permissions).toContain('execute')
  })

  it('returns error for unknown PID', async () => {
    const result = await bashKillTool.execute({ pid: 9999 }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('No background process found with PID 9999')
  })

  it('kills a process with default SIGTERM', async () => {
    const proc = makeFakeProcess(100, { command: 'sleep 60' })
    registerProcess(proc)

    const result = await bashKillTool.execute({ pid: 100 }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('Sent SIGTERM to PID 100')
    expect(result.output).toContain('sleep 60')
    expect(proc.process.kill).toHaveBeenCalledWith('SIGTERM')
  })

  it('kills a process with custom signal', async () => {
    const proc = makeFakeProcess(100)
    registerProcess(proc)

    const result = await bashKillTool.execute({ pid: 100, signal: 'SIGKILL' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('Sent SIGKILL to PID 100')
    expect(proc.process.kill).toHaveBeenCalledWith('SIGKILL')
  })

  it('removes process from registry after killing', async () => {
    const proc = makeFakeProcess(100)
    registerProcess(proc)

    await bashKillTool.execute({ pid: 100 }, makeCtx())
    expect(getProcess(100)).toBeUndefined()
  })

  it('returns error when kill throws', async () => {
    const proc = makeFakeProcess(100, { killBehavior: 'throw' })
    registerProcess(proc)

    const result = await bashKillTool.execute({ pid: 100 }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('Failed to kill PID 100')
    expect(result.error).toContain('kill failed')
  })

  it('includes metadata with pid and signal', async () => {
    const proc = makeFakeProcess(100)
    registerProcess(proc)

    const result = await bashKillTool.execute({ pid: 100, signal: 'SIGINT' }, makeCtx())
    expect(result.metadata).toEqual({ pid: 100, signal: 'SIGINT' })
  })

  it('returns error when aborted', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await bashKillTool.execute({ pid: 100 }, makeCtx({ signal: controller.signal }))
    expect(result.success).toBe(false)
    expect(result.error).toBe('Aborted')
  })

  it('uses SIGINT signal when specified', async () => {
    const proc = makeFakeProcess(200)
    registerProcess(proc)

    const result = await bashKillTool.execute({ pid: 200, signal: 'SIGINT' }, makeCtx())
    expect(result.success).toBe(true)
    expect(proc.process.kill).toHaveBeenCalledWith('SIGINT')
  })
})
