import { dispatchCompute } from '@ava/core-v2'
import { type FuzzLevel, findContext } from './four-pass-matcher.js'
import { normalizeForMatch } from './normalize-for-match.js'
import { type RaceStrategy, raceEditStrategies } from './race.js'
import { RelativeIndenter } from './relative-indenter.js'

type TierName = 'exact' | 'flexible' | 'structural' | 'four-pass' | 'fuzzy' | 'race'

interface TierResult {
  content: string
  tier: TierName
  fuzzLevel?: FuzzLevel
  similarity?: number
}

export interface EditCorrection {
  oldText: string
  newText: string
}

export interface RunEditCascadeInput {
  content: string
  oldText: string
  newText: string
  replaceAll?: boolean
  race?: boolean
  corrector?: (attempt: number, lastError: Error) => Promise<EditCorrection | null>
  maxCorrections?: number
}

export interface RunEditCascadeResult {
  content: string
  tier: TierName
  correctionsUsed: number
  fuzzLevel?: FuzzLevel
  similarity?: number
}

function escapeRegex(input: string): string {
  return input.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
}

function applyExact(
  content: string,
  oldText: string,
  newText: string,
  replaceAll: boolean
): string | null {
  if (!content.includes(oldText)) {
    return null
  }
  return replaceAll ? content.replaceAll(oldText, newText) : content.replace(oldText, newText)
}

function applyFlexible(content: string, oldText: string, newText: string): string | null {
  const contentLines = content.split('\n')
  const oldLines = oldText.split('\n')
  const oldTrimmed = oldLines.map((line) => line.trim())

  for (let i = 0; i <= contentLines.length - oldLines.length; i += 1) {
    const candidate = contentLines.slice(i, i + oldLines.length)
    const matches = candidate.every((line, offset) => line.trim() === oldTrimmed[offset])
    if (!matches) {
      continue
    }

    const sourceIndent = candidate[0]?.match(/^\s*/)?.[0] ?? ''
    const replacementLines = newText.split('\n')
    const normalizedReplacement = replacementLines.map((line, idx) => {
      if (idx === 0) {
        return line
      }
      const stripped = line.replace(/^\s*/, '')
      return `${sourceIndent}${stripped}`
    })

    const output = [...contentLines]
    output.splice(i, oldLines.length, ...normalizedReplacement)

    const indenter = new RelativeIndenter([content, oldText, newText])
    const relativeOut = indenter.makeRelative(output.join('\n'))
    return indenter.makeAbsolute(relativeOut)
  }

  return null
}

function applyStructural(content: string, oldText: string, newText: string): string | null {
  const tokens = oldText
    .split(/([(){}[\],.:=<>+\-*/])/)
    .map((token) => token.trim())
    .filter(Boolean)

  if (tokens.length < 2 || tokens.length > 30) {
    return null
  }

  const pattern = new RegExp(tokens.map(escapeRegex).join('\\s*'))
  const match = content.match(pattern)
  if (!match?.[0]) {
    return null
  }

  return content.replace(match[0], newText)
}

function normalizedSimilarity(left: string, right: string): number {
  if (left === right) {
    return 1
  }
  const a = normalizeForMatch(left)
  const b = normalizeForMatch(right)
  const maxLength = Math.max(a.length, b.length)
  if (maxLength === 0) {
    return 1
  }

  let matches = 0
  const limit = Math.min(a.length, b.length)
  for (let i = 0; i < limit; i += 1) {
    if (a[i] === b[i]) {
      matches += 1
    }
  }

  return matches / maxLength
}

function levenshtein(a: string, b: string): number {
  const rows = a.length + 1
  const cols = b.length + 1
  const matrix: number[][] = Array.from({ length: rows }, () => Array(cols).fill(0))

  for (let i = 0; i < rows; i += 1) {
    const row = matrix[i]
    if (!row) {
      continue
    }
    row[0] = i
  }
  for (let j = 0; j < cols; j += 1) {
    const firstRow = matrix[0]
    if (!firstRow) {
      continue
    }
    firstRow[j] = j
  }

  for (let i = 1; i < rows; i += 1) {
    for (let j = 1; j < cols; j += 1) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1
      const row = matrix[i]
      const prevRow = matrix[i - 1]
      if (!row || !prevRow) {
        continue
      }
      const up = prevRow[j]
      const left = row[j - 1]
      const diagonal = prevRow[j - 1]
      if (up === undefined || left === undefined || diagonal === undefined) {
        continue
      }
      row[j] = Math.min(up + 1, left + 1, diagonal + cost)
    }
  }

  const lastRow = matrix[rows - 1]
  const lastValue = lastRow?.[cols - 1]
  return lastValue ?? Math.max(a.length, b.length)
}

function similarityByLevenshtein(a: string, b: string): number {
  const normalizedA = normalizeForMatch(a)
  const normalizedB = normalizeForMatch(b)
  const maxLen = Math.max(normalizedA.length, normalizedB.length)
  if (maxLen === 0) {
    return 1
  }

  return 1 - levenshtein(normalizedA, normalizedB) / maxLen
}

function applyLevenshteinSlidingWindow(
  content: string,
  oldText: string,
  newText: string,
  threshold = 0.8
): string | null {
  const lines = content.split('\n')
  const oldLines = oldText.split('\n')
  if (oldLines.length === 0 || lines.length < oldLines.length) {
    return null
  }

  const width = oldLines.length
  let bestIndex = -1
  let bestScore = 0

  for (let i = 0; i <= lines.length - width; i += 1) {
    const candidate = lines.slice(i, i + width).join('\n')
    const score = similarityByLevenshtein(candidate, oldText)
    if (score > bestScore) {
      bestScore = score
      bestIndex = i
    }
  }

  if (bestIndex < 0 || bestScore < threshold) {
    return null
  }

  const next = [...lines]
  next.splice(bestIndex, width, ...newText.split('\n'))
  return next.join('\n')
}

function applyWholeContentRewrite(
  content: string,
  oldText: string,
  newText: string
): string | null {
  const lines = content.split('\n')
  if (lines.length > 100) {
    return null
  }

  const oldLines = oldText.split('\n')
  if (oldLines.length === 0 || lines.length < oldLines.length) {
    return null
  }

  const target = normalizeForMatch(oldText)
  let bestIndex = -1
  let bestScore = 0

  for (let i = 0; i <= lines.length - oldLines.length; i += 1) {
    const candidate = lines.slice(i, i + oldLines.length).join('\n')
    const score = normalizedSimilarity(candidate, target)
    if (score > bestScore) {
      bestScore = score
      bestIndex = i
    }
  }

  if (bestIndex < 0 || bestScore < 0.6) {
    return null
  }

  const rewritten = [...lines]
  rewritten.splice(bestIndex, oldLines.length, ...newText.split('\n'))
  return rewritten.join('\n')
}

function applyFuzzy(
  content: string,
  oldText: string,
  newText: string,
  threshold = 0.8
): string | null {
  const lines = content.split('\n')
  const oldLines = oldText.split('\n')
  if (oldLines.length === 0) {
    return null
  }

  let bestIndex = -1
  let bestScore = 0
  const windowSize = oldLines.length

  for (let i = 0; i <= lines.length - windowSize; i += 1) {
    const candidate = lines.slice(i, i + windowSize).join('\n')
    const score = normalizedSimilarity(candidate, oldText)
    if (score > bestScore) {
      bestScore = score
      bestIndex = i
    }
  }

  if (bestIndex < 0 || bestScore < threshold) {
    return null
  }

  const outputLines = [...lines]
  outputLines.splice(bestIndex, windowSize, ...newText.split('\n'))
  return outputLines.join('\n')
}

async function runTier(
  command: string,
  content: string,
  oldText: string,
  newText: string,
  tsFallback: () => string | null
): Promise<string | null> {
  try {
    return await dispatchCompute<string | null>(
      command,
      {
        content,
        oldString: oldText,
        newString: newText,
      },
      async () => tsFallback()
    )
  } catch {
    // TODO(sprint-3): replace fallback-only behavior once dedicated rust tier commands exist.
    // Tracking: issue #0.
    return tsFallback()
  }
}

async function applyCascadeSequential(
  content: string,
  oldText: string,
  newText: string,
  replaceAll: boolean
): Promise<TierResult> {
  const exact = await runTier('compute_edit_exact', content, oldText, newText, () =>
    applyExact(content, oldText, newText, replaceAll)
  )
  if (exact) {
    return { content: exact, tier: 'exact' }
  }

  const flexible = await runTier('compute_edit_flexible', content, oldText, newText, () =>
    applyFlexible(content, oldText, newText)
  )
  if (flexible) {
    return { content: flexible, tier: 'flexible' }
  }

  const structural = await runTier('compute_edit_structural', content, oldText, newText, () =>
    applyStructural(content, oldText, newText)
  )
  if (structural) {
    return { content: structural, tier: 'structural' }
  }

  const lines = content.split('\n')
  const context = oldText.split('\n')
  const fourPass = await findContext(lines, context)
  if (fourPass) {
    const updated = [...lines]
    updated.splice(fourPass.index, context.length, ...newText.split('\n'))
    return {
      content: updated.join('\n'),
      tier: 'four-pass',
      fuzzLevel: fourPass.fuzzLevel,
      similarity: fourPass.similarity,
    }
  }

  const fuzzy = applyFuzzy(content, oldText, newText)
  if (fuzzy) {
    return { content: fuzzy, tier: 'fuzzy' }
  }

  throw new Error('Edit cascade could not find a matching segment')
}

function createRaceStrategies(replaceAll: boolean): RaceStrategy[] {
  return [
    {
      name: 'exact-cascade',
      async apply(content: string, oldText: string, newText: string) {
        try {
          const result = await applyCascadeSequential(content, oldText, newText, replaceAll)
          return { content: result.content, strategy: 'exact-cascade', confidence: 0.96 }
        } catch {
          return null
        }
      },
    },
    {
      name: 'levenshtein-sliding-window',
      async apply(content: string, oldText: string, newText: string) {
        const out = applyLevenshteinSlidingWindow(content, oldText, newText, 0.8)
        if (!out) {
          return null
        }
        return { content: out, strategy: 'levenshtein-sliding-window', confidence: 0.86 }
      },
    },
    {
      name: 'ast-aware-replace',
      async apply(content: string, oldText: string, newText: string) {
        const structural = await runTier('compute_edit_structural', content, oldText, newText, () =>
          applyStructural(content, oldText, newText)
        )
        if (!structural) {
          return null
        }
        return { content: structural, strategy: 'ast-aware-replace', confidence: 0.8 }
      },
    },
    {
      name: 'whole-content-rewrite',
      async apply(content: string, oldText: string, newText: string) {
        const out = applyWholeContentRewrite(content, oldText, newText)
        if (!out) {
          return null
        }
        return { content: out, strategy: 'whole-content-rewrite', confidence: 0.65 }
      },
    },
  ]
}

async function applyCascade(
  content: string,
  oldText: string,
  newText: string,
  replaceAll: boolean,
  useRace: boolean
): Promise<TierResult> {
  if (!useRace) {
    return applyCascadeSequential(content, oldText, newText, replaceAll)
  }

  const race = await raceEditStrategies(content, oldText, newText, createRaceStrategies(replaceAll))
  return {
    content: race.content,
    tier: 'race',
  }
}

export async function runEditCascade(input: RunEditCascadeInput): Promise<RunEditCascadeResult> {
  let oldText = input.oldText
  let newText = input.newText
  const replaceAll = input.replaceAll ?? false
  const useRace = input.race ?? false
  const maxCorrections = Math.min(2, Math.max(0, input.maxCorrections ?? 2))

  try {
    const result = await applyCascade(input.content, oldText, newText, replaceAll, useRace)
    return { ...result, correctionsUsed: 0 }
  } catch (error) {
    const lastError = error instanceof Error ? error : new Error(String(error))

    if (!input.corrector || maxCorrections === 0) {
      throw lastError
    }

    let nextError: Error = lastError
    for (let attempt = 1; attempt <= maxCorrections; attempt += 1) {
      const correction = await input.corrector(attempt, nextError)
      if (!correction) {
        break
      }

      oldText = correction.oldText
      newText = correction.newText

      try {
        const result = await applyCascade(input.content, oldText, newText, replaceAll, useRace)
        return { ...result, correctionsUsed: attempt }
      } catch (inner) {
        nextError = inner instanceof Error ? inner : new Error(String(inner))
      }
    }

    throw nextError
  }
}
