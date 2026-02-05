/**
 * Edit Replacers
 * Fuzzy matching strategies for string replacement
 *
 * Based on OpenCode's edit.ts replacer pattern
 * Each replacer is a generator that yields potential matches
 */

import { normalizeUnicode } from './edit/normalize.js'

// ============================================================================
// Types
// ============================================================================

/**
 * A replacer function takes content and a search string,
 * yields potential matches from the content
 */
export type Replacer = (content: string, find: string) => Generator<string, void, unknown>

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Levenshtein distance algorithm
 * Returns the minimum number of single-character edits to transform a into b
 */
export function levenshtein(a: string, b: string): number {
  if (a === '' || b === '') {
    return Math.max(a.length, b.length)
  }

  const matrix: number[][] = Array.from({ length: a.length + 1 }, (_, i) =>
    Array.from({ length: b.length + 1 }, (_, j) => (i === 0 ? j : j === 0 ? i : 0))
  )

  for (let i = 1; i <= a.length; i++) {
    for (let j = 1; j <= b.length; j++) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1
      matrix[i][j] = Math.min(
        matrix[i - 1][j] + 1, // deletion
        matrix[i][j - 1] + 1, // insertion
        matrix[i - 1][j - 1] + cost // substitution
      )
    }
  }

  return matrix[a.length][b.length]
}

/**
 * Calculate similarity ratio between two strings (0 to 1)
 */
export function similarity(a: string, b: string): number {
  const maxLen = Math.max(a.length, b.length)
  if (maxLen === 0) return 1
  const distance = levenshtein(a, b)
  return 1 - distance / maxLen
}

/**
 * Normalize line endings (CRLF -> LF)
 */
export function normalizeLineEndings(text: string): string {
  return text.replaceAll('\r\n', '\n')
}

// ============================================================================
// Similarity Thresholds
// ============================================================================

/** When there's only one candidate, be more lenient */
const SINGLE_CANDIDATE_THRESHOLD = 0.0

/** When there are multiple candidates, require higher confidence */
const MULTIPLE_CANDIDATES_THRESHOLD = 0.3

// ============================================================================
// Replacers
// ============================================================================

/**
 * SimpleReplacer - Exact match only
 * The most strict replacer, yields the exact search string
 */
export const SimpleReplacer: Replacer = function* (_content, find) {
  yield find
}

/**
 * LineTrimmedReplacer - Match with whitespace trimmed per line
 * Useful when indentation differs but content is the same
 */
export const LineTrimmedReplacer: Replacer = function* (content, find) {
  const originalLines = content.split('\n')
  const searchLines = find.split('\n')

  // Remove trailing empty line from search if present
  if (searchLines[searchLines.length - 1] === '') {
    searchLines.pop()
  }

  for (let i = 0; i <= originalLines.length - searchLines.length; i++) {
    let matches = true

    for (let j = 0; j < searchLines.length; j++) {
      const originalTrimmed = originalLines[i + j].trim()
      const searchTrimmed = searchLines[j].trim()

      if (originalTrimmed !== searchTrimmed) {
        matches = false
        break
      }
    }

    if (matches) {
      // Calculate the actual character positions
      let matchStartIndex = 0
      for (let k = 0; k < i; k++) {
        matchStartIndex += originalLines[k].length + 1
      }

      let matchEndIndex = matchStartIndex
      for (let k = 0; k < searchLines.length; k++) {
        matchEndIndex += originalLines[i + k].length
        if (k < searchLines.length - 1) {
          matchEndIndex += 1 // newline character
        }
      }

      yield content.substring(matchStartIndex, matchEndIndex)
    }
  }
}

/**
 * BlockAnchorReplacer - Match by first/last line anchors with fuzzy middle
 * Uses Levenshtein distance to find blocks that start and end the same
 * but may have different middle content
 */
export const BlockAnchorReplacer: Replacer = function* (content, find) {
  const originalLines = content.split('\n')
  const searchLines = find.split('\n')

  // Need at least 3 lines for anchor matching
  if (searchLines.length < 3) {
    return
  }

  // Remove trailing empty line
  if (searchLines[searchLines.length - 1] === '') {
    searchLines.pop()
  }

  const firstLineSearch = searchLines[0].trim()
  const lastLineSearch = searchLines[searchLines.length - 1].trim()
  const searchBlockSize = searchLines.length

  // Collect all candidate positions
  const candidates: Array<{ startLine: number; endLine: number }> = []

  for (let i = 0; i < originalLines.length; i++) {
    if (originalLines[i].trim() !== firstLineSearch) {
      continue
    }

    // Look for matching last line
    for (let j = i + 2; j < originalLines.length; j++) {
      if (originalLines[j].trim() === lastLineSearch) {
        candidates.push({ startLine: i, endLine: j })
        break // Only first occurrence of last line
      }
    }
  }

  if (candidates.length === 0) {
    return
  }

  // Single candidate: use relaxed threshold
  if (candidates.length === 1) {
    const { startLine, endLine } = candidates[0]
    const actualBlockSize = endLine - startLine + 1
    const linesToCheck = Math.min(searchBlockSize - 2, actualBlockSize - 2)

    let avgSimilarity = 0
    if (linesToCheck > 0) {
      for (let j = 1; j < searchBlockSize - 1 && j < actualBlockSize - 1; j++) {
        const originalLine = originalLines[startLine + j].trim()
        const searchLine = searchLines[j].trim()
        avgSimilarity += similarity(originalLine, searchLine) / linesToCheck
      }
    } else {
      avgSimilarity = 1.0 // No middle lines to compare
    }

    if (avgSimilarity >= SINGLE_CANDIDATE_THRESHOLD) {
      yield extractBlock(content, originalLines, startLine, endLine)
    }
    return
  }

  // Multiple candidates: find best match
  let bestMatch: { startLine: number; endLine: number } | null = null
  let maxSimilarity = -1

  for (const candidate of candidates) {
    const { startLine, endLine } = candidate
    const actualBlockSize = endLine - startLine + 1
    const linesToCheck = Math.min(searchBlockSize - 2, actualBlockSize - 2)

    let avgSimilarity = 0
    if (linesToCheck > 0) {
      for (let j = 1; j < searchBlockSize - 1 && j < actualBlockSize - 1; j++) {
        const originalLine = originalLines[startLine + j].trim()
        const searchLine = searchLines[j].trim()
        avgSimilarity += similarity(originalLine, searchLine)
      }
      avgSimilarity /= linesToCheck
    } else {
      avgSimilarity = 1.0
    }

    if (avgSimilarity > maxSimilarity) {
      maxSimilarity = avgSimilarity
      bestMatch = candidate
    }
  }

  if (maxSimilarity >= MULTIPLE_CANDIDATES_THRESHOLD && bestMatch) {
    yield extractBlock(content, originalLines, bestMatch.startLine, bestMatch.endLine)
  }
}

/**
 * WhitespaceNormalizedReplacer - Match with all whitespace normalized
 * Collapses multiple spaces, tabs, etc. into single spaces
 */
export const WhitespaceNormalizedReplacer: Replacer = function* (content, find) {
  const normalizeWhitespace = (text: string) => text.replace(/\s+/g, ' ').trim()
  const normalizedFind = normalizeWhitespace(find)

  const lines = content.split('\n')

  // Single line matches
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    const normalizedLine = normalizeWhitespace(line)

    if (normalizedLine === normalizedFind) {
      yield line
    } else if (normalizedLine.includes(normalizedFind)) {
      // Find substring match
      const words = find.trim().split(/\s+/)
      if (words.length > 0) {
        const pattern = words.map((w) => w.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('\\s+')
        try {
          const regex = new RegExp(pattern)
          const match = line.match(regex)
          if (match) {
            yield match[0]
          }
        } catch {
          // Invalid regex, skip
        }
      }
    }
  }

  // Multi-line matches
  const findLines = find.split('\n')
  if (findLines.length > 1) {
    for (let i = 0; i <= lines.length - findLines.length; i++) {
      const block = lines.slice(i, i + findLines.length)
      if (normalizeWhitespace(block.join('\n')) === normalizedFind) {
        yield block.join('\n')
      }
    }
  }
}

/**
 * IndentationFlexibleReplacer - Match ignoring leading indentation
 * Normalizes indentation to compare content structure
 */
export const IndentationFlexibleReplacer: Replacer = function* (content, find) {
  const removeIndentation = (text: string): string => {
    const lines = text.split('\n')
    const nonEmptyLines = lines.filter((line) => line.trim().length > 0)
    if (nonEmptyLines.length === 0) return text

    const minIndent = Math.min(
      ...nonEmptyLines.map((line) => {
        const match = line.match(/^(\s*)/)
        return match ? match[1].length : 0
      })
    )

    return lines.map((line) => (line.trim().length === 0 ? line : line.slice(minIndent))).join('\n')
  }

  const normalizedFind = removeIndentation(find)
  const contentLines = content.split('\n')
  const findLines = find.split('\n')

  for (let i = 0; i <= contentLines.length - findLines.length; i++) {
    const block = contentLines.slice(i, i + findLines.length).join('\n')
    if (removeIndentation(block) === normalizedFind) {
      yield block
    }
  }
}

/**
 * TrimmedBoundaryReplacer - Match with trimmed block boundaries
 * Tries to find match when search string has extra whitespace at start/end
 */
export const TrimmedBoundaryReplacer: Replacer = function* (content, find) {
  const trimmedFind = find.trim()

  if (trimmedFind === find) {
    // Already trimmed, skip
    return
  }

  // Direct match of trimmed version
  if (content.includes(trimmedFind)) {
    yield trimmedFind
  }

  // Block matches
  const lines = content.split('\n')
  const findLines = find.split('\n')

  for (let i = 0; i <= lines.length - findLines.length; i++) {
    const block = lines.slice(i, i + findLines.length).join('\n')
    if (block.trim() === trimmedFind) {
      yield block
    }
  }
}

/**
 * MultiOccurrenceReplacer - Yields all exact matches
 * Used when replaceAll is needed
 */
export const MultiOccurrenceReplacer: Replacer = function* (content, find) {
  let startIndex = 0
  while (true) {
    const index = content.indexOf(find, startIndex)
    if (index === -1) break
    yield find
    startIndex = index + find.length
  }
}

/**
 * UnicodeNormalizedReplacer - Match with Unicode characters normalized to ASCII
 *
 * Handles common LLM quirks:
 * - Smart quotes ("") → straight quotes ("")
 * - Em/en dashes (—–) → hyphen (-)
 * - Non-breaking spaces → regular spaces
 * - Ellipsis (…) → three dots (...)
 */
export const UnicodeNormalizedReplacer: Replacer = function* (content, find) {
  const normalizedFind = normalizeUnicode(find)
  const normalizedContent = normalizeUnicode(content)

  // Skip if normalization didn't change anything
  if (normalizedFind === find && normalizedContent === content) {
    return
  }

  // Find positions in normalized content
  const lines = content.split('\n')
  const normalizedLines = normalizedContent.split('\n')
  const findLines = find.split('\n')
  const normalizedFindLines = normalizedFind.split('\n')

  // Single line match
  if (findLines.length === 1) {
    for (let i = 0; i < normalizedLines.length; i++) {
      const normalizedLine = normalizedLines[i]
      const originalLine = lines[i]

      // Check if normalized find is in normalized line
      const idx = normalizedLine.indexOf(normalizedFind)
      if (idx !== -1) {
        // Need to find corresponding position in original
        // This is tricky because character positions may differ
        // Use a heuristic: find the original substring that normalizes to the match
        const matchEnd = idx + normalizedFind.length

        // Find start position in original
        let origStart = 0
        let normPos = 0
        while (normPos < idx && origStart < originalLine.length) {
          const origChar = originalLine[origStart]
          const normChar = normalizeUnicode(origChar)
          normPos += normChar.length
          origStart++
        }

        // Find end position in original
        let origEnd = origStart
        normPos = idx
        while (normPos < matchEnd && origEnd < originalLine.length) {
          const origChar = originalLine[origEnd]
          const normChar = normalizeUnicode(origChar)
          normPos += normChar.length
          origEnd++
        }

        yield originalLine.substring(origStart, origEnd)
      }
    }
    return
  }

  // Multi-line match
  for (let i = 0; i <= normalizedLines.length - normalizedFindLines.length; i++) {
    let matches = true

    for (let j = 0; j < normalizedFindLines.length; j++) {
      // Compare trimmed normalized lines for more flexibility
      if (normalizedLines[i + j].trim() !== normalizedFindLines[j].trim()) {
        matches = false
        break
      }
    }

    if (matches) {
      // Extract original block
      yield lines.slice(i, i + findLines.length).join('\n')
    }
  }
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Extract a block of text from content given line range
 */
function extractBlock(
  content: string,
  lines: string[],
  startLine: number,
  endLine: number
): string {
  let matchStartIndex = 0
  for (let k = 0; k < startLine; k++) {
    matchStartIndex += lines[k].length + 1
  }

  let matchEndIndex = matchStartIndex
  for (let k = startLine; k <= endLine; k++) {
    matchEndIndex += lines[k].length
    if (k < endLine) {
      matchEndIndex += 1
    }
  }

  return content.substring(matchStartIndex, matchEndIndex)
}

// ============================================================================
// Default Replacer Order
// ============================================================================

/**
 * Default order of replacers from most strict to most lenient
 */
export const DEFAULT_REPLACERS: Replacer[] = [
  SimpleReplacer,
  LineTrimmedReplacer,
  UnicodeNormalizedReplacer, // Try Unicode normalization early (common LLM issue)
  BlockAnchorReplacer,
  WhitespaceNormalizedReplacer,
  IndentationFlexibleReplacer,
  TrimmedBoundaryReplacer,
  MultiOccurrenceReplacer,
]
