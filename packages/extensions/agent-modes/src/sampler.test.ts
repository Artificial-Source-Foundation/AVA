import { describe, expect, it } from 'vitest'
import {
  type SamplingCandidate,
  sampleBestOfN,
  scoreCandidate,
  selectBestCandidate,
} from './sampler.js'

describe('scoreCandidate', () => {
  it('favors successful high-quality low-cost outputs', () => {
    const strong: SamplingCandidate = {
      id: 'strong',
      success: true,
      output: 'x'.repeat(240),
      estimatedCost: 3,
    }
    const weak: SamplingCandidate = {
      id: 'weak',
      success: false,
      output: 'short',
      estimatedCost: 90,
    }

    expect(scoreCandidate(strong)).toBeGreaterThan(scoreCandidate(weak))
  })
})

describe('selectBestCandidate', () => {
  it('selects highest score candidate', () => {
    const best = selectBestCandidate([
      { id: 'a', success: false, output: 'x', estimatedCost: 1 },
      { id: 'b', success: true, output: 'good output', estimatedCost: 2 },
      { id: 'c', success: true, output: 'ok', estimatedCost: 50 },
    ])

    expect(best.id).toBe('b')
  })
})

describe('sampleBestOfN', () => {
  it('generates N candidates and returns best one', async () => {
    const byIndex: SamplingCandidate[] = [
      { id: '0', success: false, output: 'bad', estimatedCost: 5 },
      { id: '1', success: true, output: 'excellent result text', estimatedCost: 3 },
      { id: '2', success: true, output: 'ok', estimatedCost: 1 },
    ]

    const result = await sampleBestOfN({ n: 3 }, async (index) => byIndex[index]!)
    expect(result.candidates).toHaveLength(3)
    expect(result.best.id).toBe('1')
  })

  it('treats n=1 as disabled sampling', async () => {
    const result = await sampleBestOfN({ n: 1 }, async () => ({
      id: 'single',
      success: true,
      output: 'single',
      estimatedCost: 1,
    }))
    expect(result.candidates).toHaveLength(1)
    expect(result.best.id).toBe('single')
  })
})
