import { similarity } from '@ava/core-v2/tools'

import type {
  BenchmarkCase,
  BenchmarkReport,
  EditStrategyName,
  StrategyBenchmarkSummary,
  StrategyRunResult,
} from './types.js'

export const ALL_EDIT_STRATEGIES: EditStrategyName[] = [
  'exact',
  'line-trimmed',
  'whitespace-normalized',
  'block-anchor',
  'indentation-flexible',
  'replace-all',
  'fuzzy-fallback',
  'token-window',
]

function estimateTokens(text: string): number {
  return Math.ceil(text.length / 4)
}

function applyExact(content: string, c: BenchmarkCase): string | null {
  if (!content.includes(c.oldString)) return null
  return c.replaceAll
    ? content.split(c.oldString).join(c.newString)
    : content.replace(c.oldString, c.newString)
}

function applyLineTrimmed(content: string, c: BenchmarkCase): string | null {
  const contentLines = content.split('\n')
  const findLines = c.oldString.split('\n')
  for (let i = 0; i + findLines.length <= contentLines.length; i++) {
    const window = contentLines.slice(i, i + findLines.length)
    const ok = window.every((line, idx) => line.trim() === (findLines[idx] ?? '').trim())
    if (!ok) continue
    const out = [...contentLines]
    out.splice(i, findLines.length, ...c.newString.split('\n'))
    return out.join('\n')
  }
  return null
}

function normalizeWhitespace(value: string): string {
  return value.replace(/\s+/g, ' ').trim()
}

function applyWhitespaceNormalized(content: string, c: BenchmarkCase): string | null {
  const normalizedFind = normalizeWhitespace(c.oldString)
  if (!normalizedFind) return null
  const lines = content.split('\n')
  const width = c.oldString.split('\n').length
  for (let i = 0; i + width <= lines.length; i++) {
    const block = lines.slice(i, i + width).join('\n')
    if (normalizeWhitespace(block) !== normalizedFind) continue
    const out = [...lines]
    out.splice(i, width, ...c.newString.split('\n'))
    return out.join('\n')
  }
  return null
}

function applyBlockAnchor(content: string, c: BenchmarkCase): string | null {
  const oldLines = c.oldString.split('\n').filter(Boolean)
  const first = oldLines[0]
  const last = oldLines.at(-1)
  if (!first || !last) return null
  const lines = content.split('\n')
  for (let i = 0; i < lines.length; i++) {
    if (lines[i]?.trim() !== first.trim()) continue
    for (let j = i; j < lines.length; j++) {
      if (lines[j]?.trim() !== last.trim()) continue
      const out = [...lines]
      out.splice(i, j - i + 1, ...c.newString.split('\n'))
      return out.join('\n')
    }
  }
  return null
}

function applyIndentationFlexible(content: string, c: BenchmarkCase): string | null {
  const strippedFind = c.oldString
    .split('\n')
    .map((l) => l.trimStart())
    .join('\n')
  const lines = content.split('\n')
  const width = c.oldString.split('\n').length
  for (let i = 0; i + width <= lines.length; i++) {
    const window = lines
      .slice(i, i + width)
      .map((l) => l.trimStart())
      .join('\n')
    if (window !== strippedFind) continue
    const out = [...lines]
    out.splice(i, width, ...c.newString.split('\n'))
    return out.join('\n')
  }
  return null
}

function applyFuzzyFallback(content: string, c: BenchmarkCase): string | null {
  const lines = content.split('\n')
  const oldLines = c.oldString.split('\n').filter(Boolean)
  if (oldLines.length === 0) return null
  let bestIndex = -1
  let bestScore = 0
  for (let i = 0; i < lines.length; i++) {
    const score = similarity(lines[i] ?? '', oldLines[0] ?? '')
    if (score > bestScore) {
      bestScore = score
      bestIndex = i
    }
  }
  if (bestIndex === -1 || bestScore < 0.6) return null
  const out = [...lines]
  out.splice(bestIndex, oldLines.length, ...c.newString.split('\n'))
  return out.join('\n')
}

function applyTokenWindow(content: string, c: BenchmarkCase): string | null {
  const firstToken = c.oldString.split(/\s+/).find(Boolean)
  if (!firstToken) return null
  const idx = content.indexOf(firstToken)
  if (idx === -1) return null
  const windowStart = Math.max(0, idx - 200)
  const windowEnd = Math.min(content.length, idx + c.oldString.length + 200)
  const before = content.slice(0, windowStart)
  const window = content.slice(windowStart, windowEnd)
  const after = content.slice(windowEnd)
  if (!window.includes(c.oldString)) return null
  return before + window.replace(c.oldString, c.newString) + after
}

function runOne(strategy: EditStrategyName, c: BenchmarkCase): StrategyRunResult {
  const inputTokens =
    estimateTokens(c.original) + estimateTokens(c.oldString) + estimateTokens(c.newString)
  let output = c.original
  let error: string | undefined

  const maybe =
    strategy === 'exact'
      ? applyExact(c.original, c)
      : strategy === 'line-trimmed'
        ? applyLineTrimmed(c.original, c)
        : strategy === 'whitespace-normalized'
          ? applyWhitespaceNormalized(c.original, c)
          : strategy === 'block-anchor'
            ? applyBlockAnchor(c.original, c)
            : strategy === 'indentation-flexible'
              ? applyIndentationFlexible(c.original, c)
              : strategy === 'replace-all'
                ? applyExact(c.original, { ...c, replaceAll: true })
                : strategy === 'fuzzy-fallback'
                  ? applyFuzzyFallback(c.original, c)
                  : applyTokenWindow(c.original, c)

  if (maybe === null) {
    error = 'pattern_not_found'
  } else {
    output = maybe
  }

  return {
    strategy,
    caseId: c.id,
    success: maybe !== null && output === c.expected,
    output,
    inputTokens,
    outputTokens: estimateTokens(output),
    error,
  }
}

function summarize(
  strategy: EditStrategyName,
  runs: StrategyRunResult[]
): StrategyBenchmarkSummary {
  const mine = runs.filter((r) => r.strategy === strategy)
  const attempts = mine.length
  const successes = mine.filter((r) => r.success).length
  const avgInputTokens = attempts === 0 ? 0 : mine.reduce((s, r) => s + r.inputTokens, 0) / attempts
  const avgOutputTokens =
    attempts === 0 ? 0 : mine.reduce((s, r) => s + r.outputTokens, 0) / attempts
  const tokenEfficiency = successes === 0 ? 0 : successes / (avgInputTokens + avgOutputTokens)

  return {
    strategy,
    attempts,
    successes,
    successRate: attempts === 0 ? 0 : successes / attempts,
    avgInputTokens,
    avgOutputTokens,
    tokenEfficiency,
  }
}

export function runEditBenchmark(
  model: string,
  corpus: BenchmarkCase[],
  strategies: readonly EditStrategyName[] = ALL_EDIT_STRATEGIES
): BenchmarkReport {
  const runs: StrategyRunResult[] = []
  for (const strategy of strategies) {
    for (const c of corpus) {
      runs.push(runOne(strategy, c))
    }
  }

  const summaries = strategies.map((s) => summarize(s, runs))
  return {
    model,
    totalCases: corpus.length,
    totalRuns: runs.length,
    summaries,
    runs,
  }
}
