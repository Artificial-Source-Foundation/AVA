import { afterEach, describe, expect, it, vi } from 'vitest'
import { dispatchCompute } from './platform-dispatch.js'

const invokeMock = vi.fn()

vi.mock('@tauri-apps/api/core', () => ({
  invoke: invokeMock,
}))

function setWindow(value: unknown): void {
  Object.defineProperty(globalThis, 'window', {
    value,
    writable: true,
    configurable: true,
  })
}

afterEach(() => {
  invokeMock.mockReset()
  Reflect.deleteProperty(globalThis, 'window')
})

describe('dispatchCompute', () => {
  it('calls Tauri invoke when running in Tauri', async () => {
    setWindow({ __TAURI__: {} })
    invokeMock.mockResolvedValue('rust-result')
    const tsFallback = vi.fn().mockResolvedValue('ts-result')

    const result = await dispatchCompute('compute_fuzzy_replace', { content: 'abc' }, tsFallback)

    expect(result).toBe('rust-result')
    expect(invokeMock).toHaveBeenCalledWith('compute_fuzzy_replace', {
      input: { content: 'abc' },
    })
    expect(tsFallback).not.toHaveBeenCalled()
  })

  it('calls TypeScript fallback when not running in Tauri', async () => {
    const tsFallback = vi.fn().mockResolvedValue('ts-result')

    const result = await dispatchCompute('compute_fuzzy_replace', { content: 'abc' }, tsFallback)

    expect(result).toBe('ts-result')
    expect(tsFallback).toHaveBeenCalledOnce()
    expect(invokeMock).not.toHaveBeenCalled()
  })
})
