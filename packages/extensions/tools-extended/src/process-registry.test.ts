/**
 * Process registry — tracks background shell processes.
 */

import { EventEmitter } from 'node:events'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { BackgroundProcess } from './process-registry.js'
import {
  _resetRegistry,
  cleanupAll,
  getProcess,
  listProcesses,
  registerProcess,
  removeProcess,
} from './process-registry.js'

function makeFakeProcess(pid: number, command = 'echo hello'): BackgroundProcess {
  const emitter = new EventEmitter()
  return {
    pid,
    command,
    stdout: [],
    stderr: [],
    startTime: Date.now(),
    exitCode: null,
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

beforeEach(() => {
  _resetRegistry()
})

afterEach(() => {
  _resetRegistry()
})

describe('process-registry', () => {
  it('registers and retrieves a process', () => {
    const proc = makeFakeProcess(1234)
    registerProcess(proc)
    expect(getProcess(1234)).toBe(proc)
  })

  it('returns undefined for unknown PID', () => {
    expect(getProcess(9999)).toBeUndefined()
  })

  it('removes a process', () => {
    const proc = makeFakeProcess(1234)
    registerProcess(proc)
    removeProcess(1234)
    expect(getProcess(1234)).toBeUndefined()
  })

  it('lists all processes', () => {
    registerProcess(makeFakeProcess(100, 'cmd1'))
    registerProcess(makeFakeProcess(200, 'cmd2'))
    registerProcess(makeFakeProcess(300, 'cmd3'))
    const all = listProcesses()
    expect(all).toHaveLength(3)
    expect(all.map((p) => p.pid)).toEqual([100, 200, 300])
  })

  it('returns empty list when no processes', () => {
    expect(listProcesses()).toEqual([])
  })

  it('cleanupAll kills all processes and clears registry', () => {
    const proc1 = makeFakeProcess(100)
    const proc2 = makeFakeProcess(200)
    registerProcess(proc1)
    registerProcess(proc2)

    cleanupAll()

    expect(proc1.process.kill).toHaveBeenCalled()
    expect(proc2.process.kill).toHaveBeenCalled()
    expect(listProcesses()).toEqual([])
  })

  it('cleanupAll handles already-dead processes gracefully', () => {
    const proc = makeFakeProcess(100)
    ;(proc.process.kill as ReturnType<typeof vi.fn>).mockImplementation(() => {
      throw new Error('Process already dead')
    })
    registerProcess(proc)

    // Should not throw
    expect(() => cleanupAll()).not.toThrow()
    expect(listProcesses()).toEqual([])
  })

  it('overwrites process with same PID', () => {
    const proc1 = makeFakeProcess(100, 'first')
    const proc2 = makeFakeProcess(100, 'second')
    registerProcess(proc1)
    registerProcess(proc2)
    expect(getProcess(100)?.command).toBe('second')
    expect(listProcesses()).toHaveLength(1)
  })
})
