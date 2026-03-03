import { describe, expect, it } from 'vitest'

import {
  executeMultiEditJobs,
  type MultiEditApplyJob,
  type MultiEditJob,
} from './multiedit-executor.js'

function wait(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

describe('executeMultiEditJobs', () => {
  it('runs all jobs and preserves result order', async () => {
    const jobs: MultiEditJob[] = [
      { filePath: 'a.ts', edits: [] },
      { filePath: 'b.ts', edits: [] },
      { filePath: 'c.ts', edits: [] },
    ]

    const apply: MultiEditApplyJob = async (job) => ({
      filePath: job.filePath,
      success: true,
      appliedEdits: 1,
    })

    const result = await executeMultiEditJobs(jobs, apply, 3)
    expect(result.success).toBe(true)
    expect(result.results.map((r) => r.filePath)).toEqual(['a.ts', 'b.ts', 'c.ts'])
  })

  it('returns partial failure when one job fails', async () => {
    const jobs: MultiEditJob[] = [
      { filePath: 'ok.ts', edits: [] },
      { filePath: 'bad.ts', edits: [] },
    ]

    const apply: MultiEditApplyJob = async (job) => {
      if (job.filePath === 'bad.ts') {
        return { filePath: job.filePath, success: false, appliedEdits: 0, error: 'missing pattern' }
      }
      return { filePath: job.filePath, success: true, appliedEdits: 2 }
    }

    const result = await executeMultiEditJobs(jobs, apply, 2)
    expect(result.success).toBe(false)
    expect(result.succeeded).toBe(1)
    expect(result.failed).toBe(1)
    expect(result.results[1]?.error).toContain('missing pattern')
  })

  it('captures thrown errors as failed results', async () => {
    const jobs: MultiEditJob[] = [{ filePath: 'throw.ts', edits: [] }]
    const apply: MultiEditApplyJob = async () => {
      throw new Error('boom')
    }

    const result = await executeMultiEditJobs(jobs, apply, 1)
    expect(result.success).toBe(false)
    expect(result.results[0]?.error).toContain('boom')
  })

  it('respects concurrency limit', async () => {
    const jobs: MultiEditJob[] = Array.from({ length: 6 }, (_, i) => ({
      filePath: `file-${i}.ts`,
      edits: [],
    }))

    let active = 0
    let maxActive = 0
    const apply: MultiEditApplyJob = async (job) => {
      active += 1
      maxActive = Math.max(maxActive, active)
      await wait(10)
      active -= 1
      return { filePath: job.filePath, success: true, appliedEdits: 1 }
    }

    await executeMultiEditJobs(jobs, apply, 2)
    expect(maxActive).toBeLessThanOrEqual(2)
  })

  it('clamps invalid concurrency to safe minimum', async () => {
    const jobs: MultiEditJob[] = [{ filePath: 'x.ts', edits: [] }]
    const apply: MultiEditApplyJob = async (job) => ({
      filePath: job.filePath,
      success: true,
      appliedEdits: 1,
    })

    const result = await executeMultiEditJobs(jobs, apply, 0)
    expect(result.success).toBe(true)
    expect(result.results).toHaveLength(1)
  })
})
