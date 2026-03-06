import { describe, expect, it, vi } from 'vitest'
import { runEngineerWithReview } from './engineer-loop.js'

const baseContext = {
  sessionId: 's1',
  signal: new AbortController().signal,
  onEvent: vi.fn(),
  delegationDepth: 0,
  workingDirectory: process.cwd(),
}

describe('engineer review loop', () => {
  it('invokes reviewer after coding', async () => {
    const review = vi.fn().mockResolvedValue({ success: true, output: 'ok', approved: true })
    const step = vi.fn().mockResolvedValue('implemented')
    const result = await runEngineerWithReview(
      'task',
      { maxReviewAttempts: 3, autoReview: true },
      baseContext,
      step,
      review
    )
    expect(step).toHaveBeenCalledTimes(1)
    expect(review).toHaveBeenCalledTimes(1)
    expect(result.success).toBe(true)
  })

  it('retries when reviewer rejects', async () => {
    const review = vi
      .fn()
      .mockResolvedValueOnce({
        success: true,
        output: 'no',
        approved: false,
        feedback: 'fix issue',
      })
      .mockResolvedValueOnce({ success: true, output: 'yes', approved: true })
    const step = vi.fn().mockResolvedValue('implemented')
    const result = await runEngineerWithReview(
      'task',
      { maxReviewAttempts: 3, autoReview: true },
      baseContext,
      step,
      review
    )
    expect(step).toHaveBeenCalledTimes(2)
    expect(result.reviewAttempts).toBe(2)
  })

  it('enforces max review attempts', async () => {
    const review = vi.fn().mockResolvedValue({ success: true, output: 'no', approved: false })
    const step = vi.fn().mockResolvedValue('implemented')
    const result = await runEngineerWithReview(
      'task',
      { maxReviewAttempts: 3, autoReview: true },
      baseContext,
      step,
      review
    )
    expect(result.success).toBe(false)
    expect(result.reviewAttempts).toBe(3)
  })
})
