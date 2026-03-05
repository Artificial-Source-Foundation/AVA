import { dispatchCompute } from '@ava/core-v2'
import { normalizeForMatch } from './normalize-for-match.js'
import { RelativeIndenter } from './relative-indenter.js'

type TierName = 'exact' | 'flexible' | 'structural' | 'fuzzy'

interface TierResult {
  content: string
  tier: TierName
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
  corrector?: (attempt: number, lastError: Error) => Promise<EditCorrection | null>
  maxCorrections?: number
}

export interface RunEditCascadeResult {
  content: string
  tier: TierName
  correctionsUsed: number
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
    return tsFallback()
  }
}

async function applyCascade(
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

  const fuzzy = applyFuzzy(content, oldText, newText)
  if (fuzzy) {
    return { content: fuzzy, tier: 'fuzzy' }
  }

  throw new Error('Edit cascade could not find a matching segment')
}

export async function runEditCascade(input: RunEditCascadeInput): Promise<RunEditCascadeResult> {
  let oldText = input.oldText
  let newText = input.newText
  const replaceAll = input.replaceAll ?? false
  const maxCorrections = Math.min(2, Math.max(0, input.maxCorrections ?? 2))

  try {
    const result = await applyCascade(input.content, oldText, newText, replaceAll)
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
        const result = await applyCascade(input.content, oldText, newText, replaceAll)
        return { ...result, correctionsUsed: attempt }
      } catch (inner) {
        nextError = inner instanceof Error ? inner : new Error(String(inner))
      }
    }

    throw nextError
  }
}
