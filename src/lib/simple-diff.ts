/**
 * Simple Diff
 *
 * Line-by-line diff computation for sandbox review.
 * Not a true LCS diff, but sufficient for reviewing file changes.
 */

export interface DiffLine {
  type: 'added' | 'removed' | 'context'
  content: string
  lineNum: number | null
}

const CONTEXT_LINES = 3
const MAX_DIFF_LINES = 500

/**
 * Compute a simple line-by-line diff between original and modified text.
 * Returns diff lines with context collapse for readability.
 */
export function computeSimpleDiff(original: string, modified: string): DiffLine[] {
  const oldLines = original.split('\n')
  const newLines = modified.split('\n')
  const result: DiffLine[] = []

  let oldIdx = 0
  let newIdx = 0

  while (oldIdx < oldLines.length || newIdx < newLines.length) {
    if (oldIdx >= oldLines.length) {
      result.push({ type: 'added', content: newLines[newIdx], lineNum: newIdx + 1 })
      newIdx++
    } else if (newIdx >= newLines.length) {
      result.push({ type: 'removed', content: oldLines[oldIdx], lineNum: oldIdx + 1 })
      oldIdx++
    } else if (oldLines[oldIdx] === newLines[newIdx]) {
      result.push({ type: 'context', content: oldLines[oldIdx], lineNum: newIdx + 1 })
      oldIdx++
      newIdx++
    } else {
      result.push({ type: 'removed', content: oldLines[oldIdx], lineNum: oldIdx + 1 })
      result.push({ type: 'added', content: newLines[newIdx], lineNum: newIdx + 1 })
      oldIdx++
      newIdx++
    }

    if (result.length > MAX_DIFF_LINES) {
      result.push({
        type: 'context',
        content: '... (diff truncated, too many lines)',
        lineNum: null,
      })
      break
    }
  }

  return collapseContext(result)
}

/** Collapse long runs of unchanged context, keeping only lines near changes. */
function collapseContext(lines: DiffLine[]): DiffLine[] {
  const result: DiffLine[] = []

  // Find indices near non-context lines
  const keepIndices = new Set<number>()
  for (let i = 0; i < lines.length; i++) {
    if (lines[i].type !== 'context') {
      const start = Math.max(0, i - CONTEXT_LINES)
      const end = Math.min(lines.length - 1, i + CONTEXT_LINES)
      for (let j = start; j <= end; j++) keepIndices.add(j)
    }
  }

  let lastIncluded = -1
  for (let i = 0; i < lines.length; i++) {
    if (keepIndices.has(i)) {
      if (lastIncluded >= 0 && i - lastIncluded > 1) {
        result.push({
          type: 'context',
          content: `... (${i - lastIncluded - 1} lines hidden)`,
          lineNum: null,
        })
      }
      result.push(lines[i])
      lastIncluded = i
    }
  }

  // If no changes found, show first few lines
  if (result.length === 0 && lines.length > 0) {
    const show = Math.min(10, lines.length)
    for (let i = 0; i < show; i++) result.push(lines[i])
    if (lines.length > show) {
      result.push({
        type: 'context',
        content: `... (${lines.length - show} more lines)`,
        lineNum: null,
      })
    }
  }

  return result
}
