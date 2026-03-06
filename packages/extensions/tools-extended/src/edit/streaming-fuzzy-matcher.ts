import { dispatchCompute } from '@ava/core-v2'
import { normalizeForMatch } from './normalize-for-match.js'

export interface MatchResult {
  startLine: number
  endLine: number
  startOffset: number
  endOffset: number
  confidence: number
}

const INSERTION_COST = -1
const DELETION_COST = -20
const EQUALITY_BASE = 1.8

interface RustFuzzyMatchResult extends MatchResult {
  found: boolean
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

function normalizedLineSimilarity(left: string, right: string): number {
  const a = normalizeForMatch(left)
  const b = normalizeForMatch(right)
  if (a === b) {
    return 1
  }
  const maxLen = Math.max(a.length, b.length)
  if (maxLen === 0) {
    return 1
  }
  return 1 - levenshtein(a, b) / maxLen
}

function toOffsets(
  lines: string[],
  startLine: number,
  endLine: number
): { start: number; end: number } {
  let start = 0
  for (let i = 0; i < startLine; i += 1) {
    const line = lines[i] ?? ''
    start += line.length + 1
  }

  let end = start
  for (let i = startLine; i <= endLine; i += 1) {
    const line = lines[i] ?? ''
    end += line.length
    if (i < endLine) {
      end += 1
    }
  }

  return { start, end }
}

export class StreamingFuzzyMatcher {
  private readonly fileLines: string[]
  private pending = ''
  private queryLines: string[] = []
  private matrix: number[][] = [[0]]
  private equalRuns = new Map<string, number>()
  private bestMatch: MatchResult | null = null

  constructor(
    fileContent: string,
    private threshold: number = 0.8,
    private lineHint?: number
  ) {
    this.fileLines = fileContent.split('\n')
  }

  pushChunk(chunk: string): MatchResult | null {
    this.pending += chunk
    const parts = this.pending.split('\n')
    this.pending = parts.pop() ?? ''

    for (const line of parts) {
      this.queryLines.push(line)
      this.expandMatrixForLine(line)
      this.bestMatch = this.computeBestMatch()
    }

    return this.bestMatch
  }

  getBestMatch(): MatchResult | null {
    return this.bestMatch
  }

  private expandMatrixForLine(queryLine: string): void {
    const rowIndex = this.queryLines.length
    const prevRow = this.matrix[rowIndex - 1] ?? [0]
    const row = Array(this.fileLines.length + 1).fill(Number.NEGATIVE_INFINITY)
    const prevHead = prevRow[0] ?? 0
    row[0] = prevHead + DELETION_COST

    for (let col = 1; col <= this.fileLines.length; col += 1) {
      const candidate = this.fileLines[col - 1] ?? ''
      const score = normalizedLineSimilarity(queryLine, candidate)

      const up = (prevRow[col] ?? Number.NEGATIVE_INFINITY) + DELETION_COST
      const left = (row[col - 1] ?? Number.NEGATIVE_INFINITY) + INSERTION_COST

      const runKeyPrev = `${rowIndex - 1}:${col - 1}`
      const prevRun = this.equalRuns.get(runKeyPrev) ?? 0
      const isClose = score >= this.threshold
      const run = isClose ? prevRun + 1 : 0
      const diagBase = prevRow[col - 1] ?? Number.NEGATIVE_INFINITY
      const diag = isClose
        ? diagBase + EQUALITY_BASE ** Math.min(run / 4, 16)
        : diagBase + score - 1

      const best = Math.max(up, left, diag)
      row[col] = best
      if (best === diag && isClose) {
        this.equalRuns.set(`${rowIndex}:${col}`, run)
      }
    }

    this.matrix.push(row)
  }

  private computeBestMatch(): MatchResult | null {
    if (this.queryLines.length === 0 || this.queryLines.length > this.fileLines.length) {
      return null
    }

    const width = this.queryLines.length
    let bestStart = -1
    let bestConfidence = 0

    for (let start = 0; start <= this.fileLines.length - width; start += 1) {
      const candidate = this.fileLines.slice(start, start + width)
      let total = 0
      for (let i = 0; i < width; i += 1) {
        const query = this.queryLines[i] ?? ''
        const line = candidate[i] ?? ''
        total += normalizedLineSimilarity(query, line)
      }
      const confidence = total / width

      if (confidence > bestConfidence) {
        bestConfidence = confidence
        bestStart = start
      } else if (confidence === bestConfidence && this.lineHint !== undefined && bestStart >= 0) {
        const currentDistance = Math.abs(start - this.lineHint)
        const bestDistance = Math.abs(bestStart - this.lineHint)
        if (currentDistance < bestDistance) {
          bestStart = start
        }
      }
    }

    if (bestStart < 0 || bestConfidence < this.threshold) {
      return null
    }

    const endLine = bestStart + width - 1
    const offsets = toOffsets(this.fileLines, bestStart, endLine)
    return {
      startLine: bestStart,
      endLine,
      startOffset: offsets.start,
      endOffset: offsets.end,
      confidence: bestConfidence,
    }
  }
}

export async function findStreamingFuzzyMatch(
  content: string,
  query: string,
  threshold = 0.8,
  lineHint?: number
): Promise<MatchResult | null> {
  const rust = await dispatchCompute<MatchResult | RustFuzzyMatchResult | null>(
    'compute_streaming_fuzzy_match',
    {
      content,
      query,
      threshold,
      lineHint,
    },
    async () => {
      const matcher = new StreamingFuzzyMatcher(content, threshold, lineHint)
      matcher.pushChunk(`${query}\n`)
      return matcher.getBestMatch()
    }
  )

  if (!rust) {
    return null
  }
  if ('found' in rust && !rust.found) {
    return null
  }

  return {
    startLine: rust.startLine,
    endLine: rust.endLine,
    startOffset: rust.startOffset,
    endOffset: rust.endOffset,
    confidence: rust.confidence,
  }
}

export { DELETION_COST, EQUALITY_BASE, INSERTION_COST }
