import type { BenchmarkReport } from './types.js'

function pct(v: number): string {
  return `${(v * 100).toFixed(1)}%`
}

export function formatBenchmarkReport(report: BenchmarkReport): string {
  const lines: string[] = []
  lines.push(`# Edit Strategy Benchmark`)
  lines.push(``)
  lines.push(`Model: ${report.model}`)
  lines.push(`Cases: ${report.totalCases}`)
  lines.push(`Runs: ${report.totalRuns}`)
  lines.push(``)
  lines.push(`| Strategy | Success | Rate | Avg Input Tok | Avg Output Tok | Efficiency |`)
  lines.push(`|---|---:|---:|---:|---:|---:|`)

  for (const s of report.summaries) {
    lines.push(
      `| ${s.strategy} | ${s.successes}/${s.attempts} | ${pct(s.successRate)} | ${s.avgInputTokens.toFixed(1)} | ${s.avgOutputTokens.toFixed(1)} | ${s.tokenEfficiency.toFixed(5)} |`
    )
  }

  return lines.join('\n')
}
