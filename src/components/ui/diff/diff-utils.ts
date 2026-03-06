/**
 * Diff Calculation Utilities
 *
 * Line-by-line diff computation and split-view pairing logic.
 * Extracted from DiffViewer.tsx to keep each module under 300 lines.
 */

// ============================================================================
// Types
// ============================================================================

export interface DiffLine {
  type: 'add' | 'remove' | 'unchanged'
  content: string
  oldLineNumber?: number
  newLineNumber?: number
}

export interface SplitPair {
  left: DiffLine | null
  right: DiffLine | null
}

// ============================================================================
// Diff Calculation
// ============================================================================

export const computeDiff = (oldText: string, newText: string): DiffLine[] => {
  const oldLines = oldText.split('\n')
  const newLines = newText.split('\n')
  const result: DiffLine[] = []

  // Simple line-by-line diff (LCS algorithm would be better for production)
  let oldIdx = 0
  let newIdx = 0

  while (oldIdx < oldLines.length || newIdx < newLines.length) {
    const oldLine = oldLines[oldIdx]
    const newLine = newLines[newIdx]

    if (oldIdx >= oldLines.length) {
      // All remaining lines are additions
      result.push({
        type: 'add',
        content: newLine,
        newLineNumber: newIdx + 1,
      })
      newIdx++
    } else if (newIdx >= newLines.length) {
      // All remaining lines are deletions
      result.push({
        type: 'remove',
        content: oldLine,
        oldLineNumber: oldIdx + 1,
      })
      oldIdx++
    } else if (oldLine === newLine) {
      // Lines match
      result.push({
        type: 'unchanged',
        content: oldLine,
        oldLineNumber: oldIdx + 1,
        newLineNumber: newIdx + 1,
      })
      oldIdx++
      newIdx++
    } else {
      // Lines differ - check if it's a modification or insert/delete
      // Look ahead to find matching lines
      let foundInNew = false
      let foundInOld = false

      for (let i = newIdx + 1; i < Math.min(newIdx + 5, newLines.length); i++) {
        if (newLines[i] === oldLine) {
          foundInNew = true
          break
        }
      }

      for (let i = oldIdx + 1; i < Math.min(oldIdx + 5, oldLines.length); i++) {
        if (oldLines[i] === newLine) {
          foundInOld = true
          break
        }
      }

      if (foundInNew && !foundInOld) {
        // New line was inserted
        result.push({
          type: 'add',
          content: newLine,
          newLineNumber: newIdx + 1,
        })
        newIdx++
      } else if (foundInOld && !foundInNew) {
        // Old line was deleted
        result.push({
          type: 'remove',
          content: oldLine,
          oldLineNumber: oldIdx + 1,
        })
        oldIdx++
      } else {
        // Line was modified
        result.push({
          type: 'remove',
          content: oldLine,
          oldLineNumber: oldIdx + 1,
        })
        result.push({
          type: 'add',
          content: newLine,
          newLineNumber: newIdx + 1,
        })
        oldIdx++
        newIdx++
      }
    }
  }

  return result
}

// ============================================================================
// Split View Pairing
// ============================================================================

export function buildSplitPairs(lines: DiffLine[]): SplitPair[] {
  const pairs: SplitPair[] = []
  let i = 0

  while (i < lines.length) {
    const line = lines[i]

    if (line.type === 'unchanged') {
      pairs.push({ left: line, right: line })
      i++
    } else if (line.type === 'remove') {
      // Collect consecutive removes
      const removes: DiffLine[] = []
      while (i < lines.length && lines[i].type === 'remove') {
        removes.push(lines[i])
        i++
      }
      // Collect consecutive adds that follow
      const adds: DiffLine[] = []
      while (i < lines.length && lines[i].type === 'add') {
        adds.push(lines[i])
        i++
      }
      // Pair them up
      const max = Math.max(removes.length, adds.length)
      for (let j = 0; j < max; j++) {
        pairs.push({
          left: j < removes.length ? removes[j] : null,
          right: j < adds.length ? adds[j] : null,
        })
      }
    } else {
      // Standalone add (no preceding remove)
      pairs.push({ left: null, right: line })
      i++
    }
  }

  return pairs
}
