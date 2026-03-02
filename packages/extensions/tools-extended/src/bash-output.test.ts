/**
 * bash_output tool tests.
 */

import { EventEmitter } from 'node:events'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { bashOutputTool } from './bash-output.js'
import type { BackgroundProcess } from './process-registry.js'
import { _resetRegistry, registerProcess } from './process-registry.js'

function makeFakeProcess(
  pid: number,
  opts?: Partial<{
    command: string
    stdout: string[]
    stderr: string[]
    exitCode: number | null
    startTime: number
  }>
): BackgroundProcess {
  const emitter = new EventEmitter()
  return {
    pid,
    command: opts?.command ?? 'test-cmd',
    stdout: opts?.stdout ?? [],
    stderr: opts?.stderr ?? [],
    startTime: opts?.startTime ?? Date.now(),
    exitCode: opts?.exitCode ?? null,
    process: Object.assign(emitter, {
      pid,
      kill: vi.fn(),
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

describe('bashOutputTool', () => {
  it('has correct name', () => {
    expect(bashOutputTool.definition.name).toBe('bash_output')
  })

  it('has read permission', () => {
    expect(bashOutputTool.permissions).toContain('read')
  })

  it('returns error for unknown PID', async () => {
    const result = await bashOutputTool.execute({ pid: 9999 }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('No background process found with PID 9999')
  })

  it('returns stdout for a running process', async () => {
    const proc = makeFakeProcess(100, {
      stdout: ['line1', 'line2', 'line3'],
    })
    registerProcess(proc)

    const result = await bashOutputTool.execute({ pid: 100 }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('PID: 100')
    expect(result.output).toContain('Status: running')
    expect(result.output).toContain('--- stdout ---')
    expect(result.output).toContain('line1')
    expect(result.output).toContain('line2')
    expect(result.output).toContain('line3')
  })

  it('returns stderr for a running process', async () => {
    const proc = makeFakeProcess(100, {
      stderr: ['warning: something', 'error: bad'],
    })
    registerProcess(proc)

    const result = await bashOutputTool.execute({ pid: 100 }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('--- stderr ---')
    expect(result.output).toContain('warning: something')
    expect(result.output).toContain('error: bad')
  })

  it('shows exited status with exit code', async () => {
    const proc = makeFakeProcess(100, { exitCode: 1 })
    registerProcess(proc)

    const result = await bashOutputTool.execute({ pid: 100 }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('Status: exited with code 1')
  })

  it('limits output to requested number of lines', async () => {
    const lines = Array.from({ length: 100 }, (_, i) => `stdout-line-${i}`)
    const proc = makeFakeProcess(100, { stdout: lines })
    registerProcess(proc)

    const result = await bashOutputTool.execute({ pid: 100, lines: 5 }, makeCtx())
    expect(result.success).toBe(true)
    // Should only see last 5 lines
    expect(result.output).toContain('stdout-line-95')
    expect(result.output).toContain('stdout-line-99')
    expect(result.output).not.toContain('stdout-line-0')
  })

  it('defaults to 50 lines', async () => {
    const lines = Array.from({ length: 100 }, (_, i) => `line-${i}`)
    const proc = makeFakeProcess(100, { stdout: lines })
    registerProcess(proc)

    const result = await bashOutputTool.execute({ pid: 100 }, makeCtx())
    expect(result.success).toBe(true)
    // Should see line-50 through line-99 (last 50)
    expect(result.output).toContain('line-50')
    expect(result.output).toContain('line-99')
    expect(result.output).not.toContain('line-0')
    expect(result.output).not.toContain('line-49')
  })

  it('shows "(no output yet)" when buffers are empty', async () => {
    const proc = makeFakeProcess(100)
    registerProcess(proc)

    const result = await bashOutputTool.execute({ pid: 100 }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('(no output yet)')
  })

  it('includes command name in output', async () => {
    const proc = makeFakeProcess(100, { command: 'npm run dev' })
    registerProcess(proc)

    const result = await bashOutputTool.execute({ pid: 100 }, makeCtx())
    expect(result.output).toContain('Command: npm run dev')
  })

  it('includes metadata with process info', async () => {
    const proc = makeFakeProcess(100, {
      stdout: ['a', 'b'],
      stderr: ['c'],
      exitCode: 0,
    })
    registerProcess(proc)

    const result = await bashOutputTool.execute({ pid: 100 }, makeCtx())
    expect(result.metadata).toEqual({
      pid: 100,
      running: false,
      exitCode: 0,
      stdoutLines: 2,
      stderrLines: 1,
    })
  })

  it('returns error when aborted', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await bashOutputTool.execute(
      { pid: 100 },
      makeCtx({ signal: controller.signal })
    )
    expect(result.success).toBe(false)
    expect(result.error).toBe('Aborted')
  })
})
