import { dispatchCompute } from '@ava/core-v2'

export type FuzzLevel = 0 | 1 | 100 | 1000

export interface MatchResult {
  index: number
  fuzzLevel: FuzzLevel
  similarity: number
}

interface RustSimilarityResult {
  similarity: number
}

function canonicalize(text: string): string {
  return text
    .normalize('NFKC')
    .replace(/[\u2018\u2019]/g, "'")
    .replace(/[\u201C\u201D]/g, '"')
    .replace(/[\u2013\u2014]/g, '-')
}

function levenshtein(a: string, b: string): number {
  const rows = a.length + 1
  const cols = b.length + 1
  const matrix: number[][] = Array.from({ length: rows }, () => Array(cols).fill(0))

  for (let i = 0; i < rows; i += 1) {
    const row = matrix[i]
    if (row) {
      row[0] = i
    }
  }

  const firstRow = matrix[0]
  if (firstRow) {
    for (let j = 0; j < cols; j += 1) {
      firstRow[j] = j
    }
  }

  for (let i = 1; i < rows; i += 1) {
    const row = matrix[i]
    const prev = matrix[i - 1]
    if (!row || !prev) {
      continue
    }
    for (let j = 1; j < cols; j += 1) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1
      const up = prev[j] ?? Number.MAX_SAFE_INTEGER
      const left = row[j - 1] ?? Number.MAX_SAFE_INTEGER
      const diag = prev[j - 1] ?? Number.MAX_SAFE_INTEGER
      row[j] = Math.min(up + 1, left + 1, diag + cost)
    }
  }

  return matrix[rows - 1]?.[cols - 1] ?? Math.max(a.length, b.length)
}

async function calculateSimilarity(left: string, right: string): Promise<number> {
  const rust = await dispatchCompute<RustSimilarityResult | null>(
    'compute_levenshtein_similarity',
    { left, right },
    async () => {
      const maxLen = Math.max(left.length, right.length)
      if (maxLen === 0) {
        return { similarity: 1 }
      }
      const distance = levenshtein(left, right)
      return { similarity: (maxLen - distance) / maxLen }
    }
  )
  return rust?.similarity ?? 0
}

function joinWindow(
  lines: string[],
  start: number,
  width: number,
  transform?: (line: string) => string
): string {
  const chunk = lines.slice(start, start + width)
  const mapped = transform ? chunk.map(transform) : chunk
  return canonicalize(mapped.join('\n'))
}

export async function findContext(
  lines: string[],
  context: string[],
  startIdx = 0
): Promise<MatchResult | null> {
  if (context.length === 0) {
    return { index: startIdx, fuzzLevel: 0, similarity: 1 }
  }

  const width = context.length
  const contextExact = canonicalize(context.join('\n'))
  const contextTrimEnd = canonicalize(context.map((line) => line.trimEnd()).join('\n'))
  const contextTrim = canonicalize(context.map((line) => line.trim()).join('\n'))
  const maxStart = lines.length - width
  if (maxStart < startIdx) {
    return null
  }

  for (let i = startIdx; i <= maxStart; i += 1) {
    if (joinWindow(lines, i, width) === contextExact) {
      return { index: i, fuzzLevel: 0, similarity: 1 }
    }
  }

  for (let i = startIdx; i <= maxStart; i += 1) {
    if (joinWindow(lines, i, width, (line) => line.trimEnd()) === contextTrimEnd) {
      return { index: i, fuzzLevel: 1, similarity: 1 }
    }
  }

  for (let i = startIdx; i <= maxStart; i += 1) {
    if (joinWindow(lines, i, width, (line) => line.trim()) === contextTrim) {
      return { index: i, fuzzLevel: 100, similarity: 1 }
    }
  }

  let best: MatchResult | null = null
  for (let i = startIdx; i <= maxStart; i += 1) {
    const segment = joinWindow(lines, i, width)
    const similarity = await calculateSimilarity(segment, contextExact)
    if (similarity >= 0.66) {
      return { index: i, fuzzLevel: 1000, similarity }
    }
    if (!best || similarity > best.similarity) {
      best = { index: i, fuzzLevel: 1000, similarity }
    }
  }

  return null
}
