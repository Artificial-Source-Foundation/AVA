import { describe, expect, it } from 'vitest'

import { editBenchmarkTool, getDefaultCorpus, runEditBenchmark } from './index.js'
import { formatBenchmarkReport } from './reporter.js'

describe('edit benchmark harness', () => {
  it('loads a non-empty default corpus', () => {
    const corpus = getDefaultCorpus()
    expect(corpus.length).toBeGreaterThanOrEqual(5)
    expect(new Set(corpus.map((c) => c.id)).size).toBe(corpus.length)
  })

  it('runs all strategies across all cases', () => {
    const corpus = getDefaultCorpus()
    const report = runEditBenchmark('test-model', corpus)
    expect(report.totalCases).toBe(corpus.length)
    expect(report.totalRuns).toBe(corpus.length * 8)
    expect(report.summaries).toHaveLength(8)
  })

  it('computes strategy success rates and efficiency', () => {
    const report = runEditBenchmark('test-model', getDefaultCorpus())
    const exact = report.summaries.find((s) => s.strategy === 'exact')
    expect(exact).toBeDefined()
    expect((exact?.successRate ?? 0) >= 0).toBe(true)
    expect((exact?.successRate ?? 0) <= 1).toBe(true)
    expect((exact?.tokenEfficiency ?? 0) >= 0).toBe(true)
  })

  it('formats report with model and table rows', () => {
    const report = runEditBenchmark('model-x', getDefaultCorpus())
    const text = formatBenchmarkReport(report)
    expect(text).toContain('Model: model-x')
    expect(text).toContain('| Strategy | Success | Rate |')
    expect(text).toContain('exact')
  })

  it('tool executes and returns benchmark output', async () => {
    const result = await editBenchmarkTool.execute(
      { model: 'mock-1' },
      {
        sessionId: 's1',
        workingDirectory: '/tmp',
        signal: AbortSignal.timeout(5000),
      }
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('Edit Strategy Benchmark')
    expect(String((result.metadata as { model?: string }).model)).toBe('mock-1')
  })
})
