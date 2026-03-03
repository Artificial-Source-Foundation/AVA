export type EditStrategyName =
  | 'exact'
  | 'line-trimmed'
  | 'whitespace-normalized'
  | 'block-anchor'
  | 'indentation-flexible'
  | 'replace-all'
  | 'fuzzy-fallback'
  | 'token-window'

export interface BenchmarkCase {
  id: string
  description: string
  original: string
  oldString: string
  newString: string
  expected: string
  replaceAll?: boolean
}

export interface StrategyRunResult {
  strategy: EditStrategyName
  caseId: string
  success: boolean
  output: string
  inputTokens: number
  outputTokens: number
  error?: string
}

export interface StrategyBenchmarkSummary {
  strategy: EditStrategyName
  attempts: number
  successes: number
  successRate: number
  avgInputTokens: number
  avgOutputTokens: number
  tokenEfficiency: number
}

export interface BenchmarkReport {
  model: string
  totalCases: number
  totalRuns: number
  summaries: StrategyBenchmarkSummary[]
  runs: StrategyRunResult[]
}
