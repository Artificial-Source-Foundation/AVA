/**
 * Unified Diff Utilities Tests
 */

import { describe, expect, it } from 'vitest'
import {
  createDiff,
  createDiffWithHeaders,
  extractPaths,
  formatDiffLines,
  getDiffStats,
  hasChanges,
  parseDiffHunks,
} from './unified.js'

// ============================================================================
// createDiff
// ============================================================================

describe('createDiff', () => {
  it('creates a diff for single-line change', () => {
    const diff = createDiff('file.ts', 'hello\n', 'world\n')
    expect(diff).toContain('---')
    expect(diff).toContain('+++')
    expect(diff).toContain('-hello')
    expect(diff).toContain('+world')
  })

  it('creates a diff with file path in headers', () => {
    const diff = createDiff('src/main.ts', 'old\n', 'new\n')
    expect(diff).toContain('a/src/main.ts')
    expect(diff).toContain('b/src/main.ts')
  })

  it('creates empty diff for identical content', () => {
    const diff = createDiff('file.ts', 'same\n', 'same\n')
    expect(hasChanges(diff)).toBe(false)
  })

  it('handles multi-line additions', () => {
    const diff = createDiff('file.ts', 'line1\n', 'line1\nline2\nline3\n')
    expect(diff).toContain('+line2')
    expect(diff).toContain('+line3')
  })

  it('handles multi-line deletions', () => {
    const diff = createDiff('file.ts', 'line1\nline2\nline3\n', 'line1\n')
    expect(diff).toContain('-line2')
    expect(diff).toContain('-line3')
  })

  it('respects custom context lines', () => {
    const lines = `${Array.from({ length: 20 }, (_, i) => `line${i}`).join('\n')}\n`
    const modified = lines.replace('line10', 'CHANGED')
    const diff0 = createDiff('file.ts', lines, modified, 0)
    const diff5 = createDiff('file.ts', lines, modified, 5)
    // More context = more lines
    expect(diff5.split('\n').length).toBeGreaterThan(diff0.split('\n').length)
  })
})

// ============================================================================
// createDiffWithHeaders
// ============================================================================

describe('createDiffWithHeaders', () => {
  it('uses custom header paths', () => {
    const diff = createDiffWithHeaders('old/path.ts', 'new/path.ts', 'old\n', 'new\n')
    expect(diff).toContain('old/path.ts')
    expect(diff).toContain('new/path.ts')
  })
})

// ============================================================================
// parseDiffHunks
// ============================================================================

describe('parseDiffHunks', () => {
  it('parses hunks from a diff', () => {
    const diff = createDiff('file.ts', 'old\n', 'new\n')
    const hunks = parseDiffHunks(diff)
    expect(hunks.length).toBeGreaterThanOrEqual(1)
    expect(hunks[0]).toHaveProperty('oldStart')
    expect(hunks[0]).toHaveProperty('newStart')
    expect(hunks[0]).toHaveProperty('lines')
  })

  it('returns empty array for no-change diff', () => {
    const diff = createDiff('file.ts', 'same\n', 'same\n')
    const hunks = parseDiffHunks(diff)
    expect(hunks).toHaveLength(0)
  })

  it('parses multiple hunks for distant changes', () => {
    const lines = `${Array.from({ length: 30 }, (_, i) => `line${i}`).join('\n')}\n`
    const modified = lines.replace('line5', 'CHANGED5').replace('line25', 'CHANGED25')
    const diff = createDiff('file.ts', lines, modified, 1)
    const hunks = parseDiffHunks(diff)
    expect(hunks.length).toBeGreaterThanOrEqual(2)
  })
})

// ============================================================================
// getDiffStats
// ============================================================================

describe('getDiffStats', () => {
  it('counts additions and deletions', () => {
    const diff = createDiff('file.ts', 'old line\n', 'new line\n')
    const stats = getDiffStats(diff)
    expect(stats.additions).toBe(1)
    expect(stats.deletions).toBe(1)
    expect(stats.hunks).toBe(1)
  })

  it('returns zeros for no changes', () => {
    const diff = createDiff('file.ts', 'same\n', 'same\n')
    const stats = getDiffStats(diff)
    expect(stats.additions).toBe(0)
    expect(stats.deletions).toBe(0)
    expect(stats.hunks).toBe(0)
  })

  it('counts pure additions', () => {
    const diff = createDiff('file.ts', '', 'new1\nnew2\n')
    const stats = getDiffStats(diff)
    expect(stats.additions).toBe(2)
    expect(stats.deletions).toBe(0)
  })

  it('counts pure deletions', () => {
    const diff = createDiff('file.ts', 'del1\ndel2\n', '')
    const stats = getDiffStats(diff)
    expect(stats.deletions).toBe(2)
    expect(stats.additions).toBe(0)
  })
})

// ============================================================================
// hasChanges
// ============================================================================

describe('hasChanges', () => {
  it('returns true for diff with changes', () => {
    const diff = createDiff('file.ts', 'old\n', 'new\n')
    expect(hasChanges(diff)).toBe(true)
  })

  it('returns false for identical content', () => {
    const diff = createDiff('file.ts', 'same\n', 'same\n')
    expect(hasChanges(diff)).toBe(false)
  })
})

// ============================================================================
// extractPaths
// ============================================================================

describe('extractPaths', () => {
  it('extracts paths from diff headers', () => {
    const diff = createDiff('src/main.ts', 'old\n', 'new\n')
    const paths = extractPaths(diff)
    expect(paths).not.toBeNull()
    expect(paths!.oldPath).toBe('src/main.ts')
    expect(paths!.newPath).toBe('src/main.ts')
  })

  it('returns null for content without diff headers', () => {
    expect(extractPaths('just some text')).toBeNull()
  })

  it('strips a/ and b/ prefixes', () => {
    const diff = '--- a/foo.ts\n+++ b/bar.ts'
    const paths = extractPaths(diff)
    expect(paths!.oldPath).toBe('foo.ts')
    expect(paths!.newPath).toBe('bar.ts')
  })

  it('handles paths without a/b prefixes', () => {
    const diff = '--- /dev/null\n+++ new-file.ts'
    const paths = extractPaths(diff)
    expect(paths!.oldPath).toBe('/dev/null')
    expect(paths!.newPath).toBe('new-file.ts')
  })
})

// ============================================================================
// formatDiffLines
// ============================================================================

describe('formatDiffLines', () => {
  it('classifies diff line types', () => {
    const diff = createDiff('file.ts', 'old\n', 'new\n')
    const lines = formatDiffLines(diff)

    const types = new Set(lines.map((l) => l.type))
    expect(types).toContain('header')
    expect(types).toContain('hunk')
    expect(types).toContain('add')
    expect(types).toContain('remove')
  })

  it('labels --- and +++ as headers', () => {
    const lines = formatDiffLines('--- a/file.ts\n+++ b/file.ts')
    expect(lines[0].type).toBe('header')
    expect(lines[1].type).toBe('header')
  })

  it('labels @@ as hunk', () => {
    const lines = formatDiffLines('@@ -1,3 +1,3 @@')
    expect(lines[0].type).toBe('hunk')
  })

  it('labels + lines as add', () => {
    const lines = formatDiffLines('+added line')
    expect(lines[0].type).toBe('add')
  })

  it('labels - lines as remove', () => {
    const lines = formatDiffLines('-removed line')
    expect(lines[0].type).toBe('remove')
  })

  it('labels unchanged lines as context', () => {
    const lines = formatDiffLines(' context line')
    expect(lines[0].type).toBe('context')
  })
})
