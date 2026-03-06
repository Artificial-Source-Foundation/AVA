import { beforeEach, describe, expect, it, vi } from 'vitest'
import { findStreamingFuzzyMatch, StreamingFuzzyMatcher } from './streaming-fuzzy-matcher'

const { dispatchComputeMock } = vi.hoisted(() => ({ dispatchComputeMock: vi.fn() }))

vi.mock('@ava/core-v2', () => ({
  dispatchCompute: dispatchComputeMock,
}))

describe('StreamingFuzzyMatcher', () => {
  beforeEach(() => {
    dispatchComputeMock.mockReset()
    dispatchComputeMock.mockImplementation(async (_command, _args, tsFallback) => tsFallback())
  })

  it('matches exact content immediately with confidence 1.0', () => {
    const matcher = new StreamingFuzzyMatcher('const answer = 42\n')
    const result = matcher.pushChunk('const answer = 42\n')

    expect(result).not.toBeNull()
    expect(result?.confidence).toBe(1)
  })

  it('matches minor whitespace differences above threshold', () => {
    const matcher = new StreamingFuzzyMatcher('function run() {\n  return 42\n}\n')
    const result = matcher.pushChunk('function run() {\n    return 42\n}\n')

    expect(result).not.toBeNull()
    expect(result?.confidence ?? 0).toBeGreaterThanOrEqual(0.8)
  })

  it('incremental chunks converge to the correct match', () => {
    const matcher = new StreamingFuzzyMatcher('alpha\nbeta\ngamma\n')
    expect(matcher.pushChunk('alpha\n')).not.toBeNull()

    const result = matcher.pushChunk('beta\n')
    expect(result).not.toBeNull()
    expect(result?.startLine).toBe(0)
    expect(result?.endLine).toBe(1)
  })

  it('returns null when confidence is below threshold', () => {
    const matcher = new StreamingFuzzyMatcher('first line\nsecond line\n', 0.9)
    const result = matcher.pushChunk('totally unrelated\n')

    expect(result).toBeNull()
  })

  it('prefers contiguous matches over scattered lines', () => {
    const content = ['target start', 'noise', 'target end', 'target start', 'target end'].join('\n')
    const matcher = new StreamingFuzzyMatcher(content)
    const result = matcher.pushChunk('target start\ntarget end\n')

    expect(result).not.toBeNull()
    expect(result?.startLine).toBe(3)
    expect(result?.endLine).toBe(4)
  })

  it('findStreamingFuzzyMatch uses dispatchCompute fallback path', async () => {
    const result = await findStreamingFuzzyMatch('x\ny\nz\n', 'y', 0.8)

    expect(result).not.toBeNull()
    expect(dispatchComputeMock).toHaveBeenCalledWith(
      'compute_streaming_fuzzy_match',
      expect.objectContaining({ query: 'y' }),
      expect.any(Function)
    )
  })
})
