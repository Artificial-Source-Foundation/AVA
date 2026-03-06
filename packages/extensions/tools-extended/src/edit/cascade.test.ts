import { beforeEach, describe, expect, it, vi } from 'vitest'
import { runEditCascade } from './cascade'

const { dispatchComputeMock } = vi.hoisted(() => ({ dispatchComputeMock: vi.fn() }))

vi.mock('@ava/core-v2', () => ({
  dispatchCompute: dispatchComputeMock,
}))

describe('runEditCascade', () => {
  beforeEach(() => {
    dispatchComputeMock.mockReset()
    dispatchComputeMock.mockImplementation(async (_command, _args, tsFallback) => tsFallback())
  })

  it('applies exact replacement on tier 1', async () => {
    const result = await runEditCascade({
      content: 'hello world',
      oldText: 'world',
      newText: 'ava',
    })

    expect(result.content).toBe('hello ava')
    expect(result.tier).toBe('exact')
  })

  it('uses flexible tier with indentation-aware matching', async () => {
    const result = await runEditCascade({
      content: 'function run() {\n    return 1\n}\n',
      oldText: 'function run() {\n  return 1\n}\n',
      newText: 'function run() {\n  return 2\n}\n',
    })

    expect(result.content).toContain('return 2')
    expect(result.tier).toBe('flexible')
  })

  it('uses four-pass tier for near matches after structural', async () => {
    const result = await runEditCascade({
      content: 'const greeting = "hullo world"\n',
      oldText: 'const greeting = "hello world"\n',
      newText: 'const greeting = "hello ava"\n',
    })

    expect(result.content).toContain('hello ava')
    expect(result.tier).toBe('four-pass')
    expect(result.fuzzLevel).toBe(1000)
  })

  it('retries with corrector at most twice', async () => {
    const corrector = vi
      .fn()
      .mockResolvedValueOnce({ oldText: 'world', newText: 'AVA' })
      .mockResolvedValueOnce(null)

    const result = await runEditCascade({
      content: 'hello world',
      oldText: 'wurld',
      newText: 'ava',
      corrector,
      maxCorrections: 2,
    })

    expect(result.content).toBe('hello AVA')
    expect(result.correctionsUsed).toBe(1)
    expect(corrector).toHaveBeenCalledTimes(1)
  })

  it('calls dispatchCompute for first three tiers', async () => {
    await runEditCascade({
      content: 'prefix\nconst greeting = "hullo world"\nsuffix\n',
      oldText: 'const greeting = "hello world"',
      newText: 'const greeting = "hello ava"',
    })

    expect(dispatchComputeMock).toHaveBeenCalledWith(
      'compute_edit_exact',
      expect.objectContaining({ oldString: 'const greeting = "hello world"' }),
      expect.any(Function)
    )
    expect(dispatchComputeMock).toHaveBeenCalledWith(
      'compute_edit_flexible',
      expect.any(Object),
      expect.any(Function)
    )
    expect(dispatchComputeMock).toHaveBeenCalledWith(
      'compute_edit_structural',
      expect.any(Object),
      expect.any(Function)
    )
  })

  it('uses race tier when race flag is enabled', async () => {
    const result = await runEditCascade({
      content: 'hello world',
      oldText: 'world',
      newText: 'ava',
      race: true,
    })

    expect(result.content).toBe('hello ava')
    expect(result.tier).toBe('race')
  })
})
