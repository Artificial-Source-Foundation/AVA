import { beforeEach, describe, expect, it, vi } from 'vitest'
import {
  abortExecutor,
  clearExecutorRegistry,
  getAllExecutors,
  getExecutor,
  registerExecutor,
  unregisterExecutor,
} from './executor-registry.js'

describe('executor-registry', () => {
  const mockExecutor = {} as import('./loop.js').AgentExecutor

  beforeEach(() => {
    clearExecutorRegistry()
  })

  it('registers and retrieves an executor', () => {
    const abort = new AbortController()
    registerExecutor('test-1', mockExecutor, abort, null, 'test')

    const entry = getExecutor('test-1')
    expect(entry).toBeDefined()
    expect(entry!.executor).toBe(mockExecutor)
    expect(entry!.abort).toBe(abort)
    expect(entry!.parentId).toBe(null)
    expect(entry!.name).toBe('test')
    expect(entry!.startedAt).toBeGreaterThan(0)
  })

  it('unregisters an executor', () => {
    const abort = new AbortController()
    registerExecutor('test-1', mockExecutor, abort)
    unregisterExecutor('test-1')

    expect(getExecutor('test-1')).toBeUndefined()
  })

  it('aborts a running executor', () => {
    const abort = new AbortController()
    registerExecutor('test-1', mockExecutor, abort)

    const abortSpy = vi.spyOn(abort, 'abort')
    const result = abortExecutor('test-1')

    expect(result).toBe(true)
    expect(abortSpy).toHaveBeenCalled()
  })

  it('returns false when aborting non-existent executor', () => {
    expect(abortExecutor('non-existent')).toBe(false)
  })

  it('lists all executors', () => {
    registerExecutor('a', mockExecutor, new AbortController())
    registerExecutor('b', mockExecutor, new AbortController())

    const all = getAllExecutors()
    expect(all.size).toBe(2)
    expect(all.has('a')).toBe(true)
    expect(all.has('b')).toBe(true)
  })

  it('stores parentId', () => {
    registerExecutor('child', mockExecutor, new AbortController(), 'parent-id', 'child-name')

    const entry = getExecutor('child')
    expect(entry!.parentId).toBe('parent-id')
    expect(entry!.name).toBe('child-name')
  })

  it('clears all executors', () => {
    registerExecutor('a', mockExecutor, new AbortController())
    registerExecutor('b', mockExecutor, new AbortController())

    clearExecutorRegistry()
    expect(getAllExecutors().size).toBe(0)
  })
})
