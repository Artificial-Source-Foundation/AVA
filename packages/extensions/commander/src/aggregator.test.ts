import { describe, expect, it } from 'vitest'
import { aggregateResults } from './aggregator.js'
import type { SubtaskResult } from './orchestrator.js'

function makeSubtaskResult(overrides?: Partial<SubtaskResult>): SubtaskResult {
  return {
    subtaskId: '0',
    agentId: 'frontend-lead',
    success: true,
    output: 'Done',
    durationMs: 100,
    ...overrides,
  }
}

describe('aggregateResults', () => {
  it('counts succeeded and failed subtasks', () => {
    const results: SubtaskResult[] = [
      makeSubtaskResult({ success: true }),
      makeSubtaskResult({ subtaskId: '1', success: false, output: 'Error occurred' }),
      makeSubtaskResult({ subtaskId: '2', success: true }),
    ]

    const agg = aggregateResults(results)

    expect(agg.totalSubtasks).toBe(3)
    expect(agg.succeeded).toBe(2)
    expect(agg.failed).toBe(1)
  })

  it('extracts file changes from output', () => {
    const results: SubtaskResult[] = [
      makeSubtaskResult({
        output: "created 'src/Login.tsx' and modified 'src/App.tsx'",
      }),
      makeSubtaskResult({
        subtaskId: '1',
        output: "edited 'src/api/auth.ts'",
      }),
    ]

    const agg = aggregateResults(results)

    expect(agg.filesChanged).toContain('src/Login.tsx')
    expect(agg.filesChanged).toContain('src/App.tsx')
    expect(agg.filesChanged).toContain('src/api/auth.ts')
    expect(agg.filesChanged).toHaveLength(3)
  })

  it('deduplicates file changes', () => {
    const results: SubtaskResult[] = [
      makeSubtaskResult({ output: "modified 'src/App.tsx'" }),
      makeSubtaskResult({ subtaskId: '1', output: "edited 'src/App.tsx'" }),
    ]

    const agg = aggregateResults(results)

    expect(agg.filesChanged).toEqual(['src/App.tsx'])
  })

  it('extracts test results', () => {
    const results: SubtaskResult[] = [
      makeSubtaskResult({
        output: '12 tests passed. 15 tests total.',
      }),
      makeSubtaskResult({
        subtaskId: '1',
        output: '8 specs passed. 10 tests run.',
      }),
    ]

    const agg = aggregateResults(results)

    expect(agg.testsPassed).toBe(20) // 12 + 8
    expect(agg.testsRun).toBe(25) // 15 + 10
  })

  it('collects issues from failed subtasks', () => {
    const results: SubtaskResult[] = [
      makeSubtaskResult({ success: true }),
      makeSubtaskResult({
        subtaskId: '1',
        agentId: 'backend-lead',
        success: false,
        output: 'Type error in auth module: cannot assign string to number',
      }),
      makeSubtaskResult({
        subtaskId: '2',
        agentId: 'qa-lead',
        success: false,
        output: 'Test suite failed: 3 failures',
      }),
    ]

    const agg = aggregateResults(results)

    expect(agg.issuesFound).toHaveLength(2)
    expect(agg.issuesFound[0]).toContain('backend-lead')
    expect(agg.issuesFound[0]).toContain('Type error')
    expect(agg.issuesFound[1]).toContain('qa-lead')
  })

  it('calculates total duration', () => {
    const results: SubtaskResult[] = [
      makeSubtaskResult({ durationMs: 100 }),
      makeSubtaskResult({ subtaskId: '1', durationMs: 250 }),
      makeSubtaskResult({ subtaskId: '2', durationMs: 150 }),
    ]

    const agg = aggregateResults(results)

    expect(agg.durationMs).toBe(500)
  })

  it('builds a readable summary', () => {
    const results: SubtaskResult[] = [
      makeSubtaskResult({
        output: "created 'src/utils.ts'. 5 tests passed. 5 tests total.",
        durationMs: 200,
      }),
      makeSubtaskResult({
        subtaskId: '1',
        agentId: 'backend-lead',
        success: false,
        output: 'Connection refused',
        durationMs: 50,
      }),
    ]

    const agg = aggregateResults(results)

    expect(agg.summary).toContain('## Aggregated Results')
    expect(agg.summary).toContain('1/2 subtasks succeeded')
    expect(agg.summary).toContain('1 files changed')
    expect(agg.summary).toContain('5/5 tests passed')
    expect(agg.summary).toContain('1 issues found')
    expect(agg.summary).toContain('Total duration: 250ms')
  })

  it('handles empty results', () => {
    const agg = aggregateResults([])

    expect(agg.totalSubtasks).toBe(0)
    expect(agg.succeeded).toBe(0)
    expect(agg.failed).toBe(0)
    expect(agg.filesChanged).toEqual([])
    expect(agg.testsRun).toBe(0)
    expect(agg.testsPassed).toBe(0)
    expect(agg.issuesFound).toEqual([])
    expect(agg.durationMs).toBe(0)
  })

  it('handles output with no extractable patterns', () => {
    const results: SubtaskResult[] = [makeSubtaskResult({ output: 'Task completed successfully' })]

    const agg = aggregateResults(results)

    expect(agg.filesChanged).toEqual([])
    expect(agg.testsRun).toBe(0)
    expect(agg.testsPassed).toBe(0)
  })

  it('handles files with special characters in path', () => {
    const results: SubtaskResult[] = [
      makeSubtaskResult({
        output: "created 'src/@ava/core-v2/index.ts'",
      }),
    ]

    const agg = aggregateResults(results)

    expect(agg.filesChanged).toContain('src/@ava/core-v2/index.ts')
  })
})
