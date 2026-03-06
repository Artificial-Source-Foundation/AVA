import { beforeEach, describe, expect, it, vi } from 'vitest'
import { type RaceStrategy, raceEditStrategies } from './race'

const { dispatchComputeMock } = vi.hoisted(() => ({ dispatchComputeMock: vi.fn() }))

vi.mock('@ava/core-v2', () => ({
  dispatchCompute: dispatchComputeMock,
}))

describe('raceEditStrategies', () => {
  beforeEach(() => {
    dispatchComputeMock.mockReset()
    dispatchComputeMock.mockResolvedValue({ valid: true })
  })

  it('returns the first valid strategy result', async () => {
    const strategies: RaceStrategy[] = [
      {
        name: 'exact',
        apply: vi.fn().mockResolvedValue({
          content: 'hello ava',
          strategy: 'exact',
          confidence: 1,
        }),
      },
      {
        name: 'slow',
        apply: vi.fn(async () => {
          await new Promise((resolve) => setTimeout(resolve, 50))
          return { content: 'hello slow', strategy: 'slow', confidence: 0.6 }
        }),
      },
    ]

    const result = await raceEditStrategies('hello world', 'world', 'ava', strategies)

    expect(result.strategy).toBe('exact')
    expect(result.content).toBe('hello ava')
  })

  it('falls back to a later strategy when earlier one fails', async () => {
    const strategies: RaceStrategy[] = [
      {
        name: 'exact',
        apply: vi.fn().mockResolvedValue(null),
      },
      {
        name: 'fuzzy',
        apply: vi.fn().mockResolvedValue({
          content: 'const greeting = "hello ava"',
          strategy: 'fuzzy',
          confidence: 0.82,
        }),
      },
    ]

    const result = await raceEditStrategies(
      'const greeting = "hullo world"',
      'const greeting = "hello world"',
      'hello ava',
      strategies
    )

    expect(result.strategy).toBe('fuzzy')
    expect(result.confidence).toBeGreaterThanOrEqual(0.8)
  })

  it('uses another strategy when one times out', async () => {
    const strategies: RaceStrategy[] = [
      {
        name: 'hangs',
        apply: vi.fn(async () => {
          throw new Error('hangs timed out after 5000ms')
        }),
      },
      {
        name: 'whole-file',
        apply: vi.fn(async () => {
          await new Promise((resolve) => setTimeout(resolve, 25))
          return {
            content: 'line 1\nline 2 updated\nline 3',
            strategy: 'whole-file',
            confidence: 0.7,
          }
        }),
      },
    ]

    const result = await raceEditStrategies(
      'line 1\nline 2\nline 3',
      'line 2',
      'line 2 updated',
      strategies
    )

    expect(result.strategy).toBe('whole-file')
  })

  it('throws with all attempted strategy names when all fail', async () => {
    const strategies: RaceStrategy[] = [
      { name: 'exact', apply: vi.fn().mockResolvedValue(null) },
      { name: 'fuzzy', apply: vi.fn().mockRejectedValue(new Error('boom')) },
    ]

    await expect(raceEditStrategies('a', 'b', 'c', strategies)).rejects.toThrow(/exact, fuzzy/)
  })
})
