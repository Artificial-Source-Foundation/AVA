/**
 * Edit replacement strategies — ordered from strict to lenient.
 *
 * Each replacer is a generator that yields candidate substrings from content
 * that match the search string. The edit tool tries them in order.
 */

// ─── Types ───────────────────────────────────────────────────────────────────

export type Replacer = (content: string, find: string) => Generator<string>

// ─── Utility ─────────────────────────────────────────────────────────────────

function normalizeLineEndings(text: string): string {
  return text.replace(/\r\n/g, '\n')
}

function levenshtein(a: string, b: string): number {
  const m = a.length
  const n = b.length
  const dp: number[][] = Array.from({ length: m + 1 }, () => Array(n + 1).fill(0) as number[])

  for (let i = 0; i <= m; i++) dp[i][0] = i
  for (let j = 0; j <= n; j++) dp[0][j] = j

  for (let i = 1; i <= m; i++) {
    for (let j = 1; j <= n; j++) {
      dp[i][j] =
        a[i - 1] === b[j - 1]
          ? dp[i - 1][j - 1]
          : 1 + Math.min(dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1])
    }
  }
  return dp[m][n]
}

function similarity(a: string, b: string): number {
  const maxLen = Math.max(a.length, b.length)
  if (maxLen === 0) return 1
  return 1 - levenshtein(a, b) / maxLen
}

// ─── Replacers ───────────────────────────────────────────────────────────────

function* simpleReplacer(content: string, find: string): Generator<string> {
  if (content.includes(find)) yield find
}

function* lineTrimmedReplacer(content: string, find: string): Generator<string> {
  const contentLines = content.split('\n')
  const searchLines = find.split('\n')
  if (searchLines.length === 0) return

  for (let i = 0; i <= contentLines.length - searchLines.length; i++) {
    let matches = true
    for (let j = 0; j < searchLines.length; j++) {
      if (contentLines[i + j].trim() !== searchLines[j].trim()) {
        matches = false
        break
      }
    }
    if (matches) {
      yield contentLines.slice(i, i + searchLines.length).join('\n')
    }
  }
}

function* whitespaceNormalizedReplacer(content: string, find: string): Generator<string> {
  const normalizedContent = content.replace(/\s+/g, ' ')
  const normalizedFind = find.replace(/\s+/g, ' ')
  const idx = normalizedContent.indexOf(normalizedFind)
  if (idx === -1) return

  // Map back to original positions
  let origIdx = 0
  let normIdx = 0
  while (normIdx < idx) {
    if (/\s/.test(content[origIdx])) {
      while (origIdx < content.length && /\s/.test(content[origIdx])) origIdx++
      normIdx++
    } else {
      origIdx++
      normIdx++
    }
  }
  const startOrig = origIdx

  let remaining = normalizedFind.length
  while (remaining > 0 && origIdx < content.length) {
    if (/\s/.test(content[origIdx])) {
      while (origIdx < content.length && /\s/.test(content[origIdx])) origIdx++
      remaining--
    } else {
      origIdx++
      remaining--
    }
  }

  yield content.slice(startOrig, origIdx)
}

function* blockAnchorReplacer(content: string, find: string): Generator<string> {
  const contentLines = content.split('\n')
  const searchLines = find.split('\n')
  if (searchLines.length < 3) return

  const firstSearch = searchLines[0].trim()
  const lastSearch = searchLines[searchLines.length - 1].trim()

  const candidates: Array<{ start: number; end: number; score: number }> = []

  for (let i = 0; i <= contentLines.length - searchLines.length; i++) {
    if (contentLines[i].trim() !== firstSearch) continue
    const endIdx = i + searchLines.length - 1
    if (endIdx >= contentLines.length) continue
    if (contentLines[endIdx].trim() !== lastSearch) continue

    // Score middle lines by similarity
    let totalSim = 0
    for (let j = 1; j < searchLines.length - 1; j++) {
      totalSim += similarity(contentLines[i + j].trim(), searchLines[j].trim())
    }
    const avgSim = totalSim / Math.max(1, searchLines.length - 2)
    candidates.push({ start: i, end: endIdx, score: avgSim })
  }

  // Pick best candidate
  if (candidates.length === 1) {
    const c = candidates[0]
    yield contentLines.slice(c.start, c.end + 1).join('\n')
  } else if (candidates.length > 1) {
    candidates.sort((a, b) => b.score - a.score)
    if (candidates[0].score >= 0.3) {
      const c = candidates[0]
      yield contentLines.slice(c.start, c.end + 1).join('\n')
    }
  }
}

function* indentationFlexibleReplacer(content: string, find: string): Generator<string> {
  const contentLines = content.split('\n')
  const searchLines = find.split('\n')
  if (searchLines.length === 0) return

  // Strip common indentation from search
  const searchIndent = searchLines
    .filter((l) => l.trim().length > 0)
    .reduce((min, l) => Math.min(min, l.length - l.trimStart().length), Infinity)
  const strippedSearch = searchLines.map((l) =>
    l.trim().length === 0 ? '' : l.slice(searchIndent)
  )

  for (let i = 0; i <= contentLines.length - searchLines.length; i++) {
    const block = contentLines.slice(i, i + searchLines.length)
    const blockIndent = block
      .filter((l) => l.trim().length > 0)
      .reduce((min, l) => Math.min(min, l.length - l.trimStart().length), Infinity)
    const strippedBlock = block.map((l) => (l.trim().length === 0 ? '' : l.slice(blockIndent)))

    if (strippedBlock.every((line, j) => line === strippedSearch[j])) {
      yield block.join('\n')
    }
  }
}

// ─── Default Replacer Chain ──────────────────────────────────────────────────

export const DEFAULT_REPLACERS: Replacer[] = [
  simpleReplacer,
  lineTrimmedReplacer,
  whitespaceNormalizedReplacer,
  blockAnchorReplacer,
  indentationFlexibleReplacer,
]

// ─── Replace Function ────────────────────────────────────────────────────────

export function replace(
  content: string,
  oldString: string,
  newString: string,
  replaceAll: boolean
): string {
  const normalizedContent = normalizeLineEndings(content)
  const normalizedOld = normalizeLineEndings(oldString)

  if (replaceAll) {
    // Simple replaceAll for multi-occurrence
    if (normalizedContent.includes(normalizedOld)) {
      return normalizedContent.split(normalizedOld).join(newString)
    }
  }

  for (const replacer of DEFAULT_REPLACERS) {
    for (const candidate of replacer(normalizedContent, normalizedOld)) {
      if (replaceAll) {
        return normalizedContent.split(candidate).join(newString)
      }

      // For single replace: ensure uniqueness
      const firstIdx = normalizedContent.indexOf(candidate)
      const lastIdx = normalizedContent.lastIndexOf(candidate)

      if (firstIdx !== lastIdx) {
        // Multiple matches — try next replacer
        continue
      }

      return (
        normalizedContent.slice(0, firstIdx) +
        newString +
        normalizedContent.slice(firstIdx + candidate.length)
      )
    }
  }

  throw new Error('oldString not found in file content. Verify the text matches exactly.')
}
