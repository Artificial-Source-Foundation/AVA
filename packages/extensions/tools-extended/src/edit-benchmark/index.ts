import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'

import { getDefaultCorpus } from './corpus.js'
import { formatBenchmarkReport } from './reporter.js'
import { ALL_EDIT_STRATEGIES, runEditBenchmark } from './runner.js'
import type { EditStrategyName } from './types.js'

const StrategyEnum = z.enum(ALL_EDIT_STRATEGIES)

const Schema = z.object({
  model: z.string().default('mock-model'),
  strategies: z.array(StrategyEnum).optional(),
})

export const editBenchmarkTool = defineTool({
  name: 'edit_benchmark',
  description: 'Benchmark edit strategies against corpus diffs and report success/token efficiency',
  schema: Schema,
  permissions: ['read'],
  async execute(input) {
    const strategies = input.strategies as EditStrategyName[] | undefined
    const report = runEditBenchmark(input.model, getDefaultCorpus(), strategies)
    return {
      success: true,
      output: formatBenchmarkReport(report),
      metadata: {
        model: report.model,
        totalCases: report.totalCases,
        totalRuns: report.totalRuns,
        summaries: report.summaries,
      },
    }
  },
})

export { getDefaultCorpus } from './corpus.js'
export { formatBenchmarkReport } from './reporter.js'
export { ALL_EDIT_STRATEGIES, runEditBenchmark } from './runner.js'
export type { BenchmarkCase, BenchmarkReport, EditStrategyName } from './types.js'
