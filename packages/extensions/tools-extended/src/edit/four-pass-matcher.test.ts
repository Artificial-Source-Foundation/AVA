import { beforeEach, describe, expect, it, vi } from 'vitest'
import { findContext } from './four-pass-matcher'

const { dispatchComputeMock } = vi.hoisted(() => ({ dispatchComputeMock: vi.fn() }))

vi.mock('@ava/core-v2', () => ({
  dispatchCompute: dispatchComputeMock,
}))

describe('findContext', () => {
  beforeEach(() => {
    dispatchComputeMock.mockReset()
    dispatchComputeMock.mockImplementation(async (_command, _args, tsFallback) => tsFallback())
  })

  it('returns fuzz=0 on exact match', async () => {
    const result = await findContext(['a', 'b', 'c'], ['b'])
    expect(result).toMatchObject({ index: 1, fuzzLevel: 0 })
  })

  it('returns fuzz=1 when only trailing whitespace differs', async () => {
    const result = await findContext(['hello   '], ['hello'])
    expect(result).toMatchObject({ index: 0, fuzzLevel: 1 })
  })

  it('returns fuzz=100 when leading whitespace differs', async () => {
    const result = await findContext(['    return x'], ['return x'])
    expect(result).toMatchObject({ index: 0, fuzzLevel: 100 })
  })

  it('returns fuzz=1000 for similarity above threshold', async () => {
    const result = await findContext(['hello world!'], ['hello world'])
    expect(result).not.toBeNull()
    expect(result?.fuzzLevel).toBe(1000)
    expect(result?.similarity ?? 0).toBeGreaterThanOrEqual(0.66)
  })

  it('returns null when similarity is below threshold', async () => {
    const result = await findContext(['abc'], ['xyz'])
    expect(result).toBeNull()
  })

  it('normalizes smart quotes and em dashes', async () => {
    const result = await findContext(['“hello” — world'], ['"hello" - world'])
    expect(result).toMatchObject({ index: 0, fuzzLevel: 0 })
  })
})
