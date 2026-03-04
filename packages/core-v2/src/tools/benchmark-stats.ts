export interface BenchmarkSummary {
  iterations: number
  min: number
  max: number
  p50: number
  p95: number
  mean: number
}

export function percentile(values: number[], p: number): number {
  if (values.length === 0) {
    throw new Error('Cannot compute percentile of empty list')
  }

  const sorted = [...values].sort((a, b) => a - b)
  const clamped = Math.max(0, Math.min(100, p))
  const rank = Math.ceil((clamped / 100) * sorted.length)
  const index = Math.max(0, Math.min(sorted.length - 1, rank - 1))

  return sorted[index]
}

export function summarizeSamples(samples: number[]): BenchmarkSummary {
  if (samples.length === 0) {
    throw new Error('Cannot summarize empty sample list')
  }

  const min = samples.reduce((acc, sample) => Math.min(acc, sample), Number.POSITIVE_INFINITY)
  const max = samples.reduce((acc, sample) => Math.max(acc, sample), Number.NEGATIVE_INFINITY)
  const total = samples.reduce((sum, value) => sum + value, 0)
  const mean = total / samples.length

  return {
    iterations: samples.length,
    min,
    max,
    p50: percentile(samples, 50),
    p95: percentile(samples, 95),
    mean,
  }
}
