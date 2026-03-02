/**
 * Result aggregation for multi-lead delegation.
 *
 * Parses subtask outputs to extract file changes, test results,
 * and issues, then produces a unified summary.
 */

import type { SubtaskResult } from './orchestrator.js'

export interface AggregatedResult {
  totalSubtasks: number
  succeeded: number
  failed: number
  filesChanged: string[]
  testsRun: number
  testsPassed: number
  issuesFound: string[]
  summary: string
  durationMs: number
}

/**
 * Aggregate an array of SubtaskResults into a single report.
 *
 * Extracts:
 * - Files changed (from output patterns like `created 'path'`, `modified 'path'`)
 * - Test counts (from patterns like `N tests passed`)
 * - Issues (failed subtask outputs)
 */
export function aggregateResults(results: SubtaskResult[]): AggregatedResult {
  const filesChanged = new Set<string>()
  let testsRun = 0
  let testsPassed = 0
  const issuesFound: string[] = []
  let totalDuration = 0

  for (const r of results) {
    totalDuration += r.durationMs

    // Extract file changes from output
    const fileMatches = r.output.match(
      /(?:created|modified|edited|wrote)\s+[`'"]([\w/.@-]+)[`'"]/gi
    )
    if (fileMatches) {
      for (const m of fileMatches) {
        const file = m.match(/[`'"]([\w/.@-]+)[`'"]/)?.[1]
        if (file) filesChanged.add(file)
      }
    }

    // Extract test results — "N tests passed" or "N specs passed"
    const passedMatch = r.output.match(/(\d+)\s+(?:tests?|specs?)\s+passed/i)
    if (passedMatch) {
      testsPassed += parseInt(passedMatch[1]!, 10)
    }
    const totalTestMatch = r.output.match(/(\d+)\s+(?:tests?|specs?)\s+(?:total|run)/i)
    if (totalTestMatch) {
      testsRun += parseInt(totalTestMatch[1]!, 10)
    }

    // Collect issues from failed subtasks
    if (!r.success) {
      issuesFound.push(`${r.agentId}: ${r.output.slice(0, 200)}`)
    }
  }

  const succeeded = results.filter((r) => r.success).length
  const failed = results.filter((r) => !r.success).length

  const summaryLines: string[] = [
    `## Aggregated Results`,
    `- ${succeeded}/${results.length} subtasks succeeded`,
  ]

  if (filesChanged.size > 0) {
    summaryLines.push(`- ${filesChanged.size} files changed: ${[...filesChanged].join(', ')}`)
  }
  if (testsRun > 0) {
    summaryLines.push(`- ${testsPassed}/${testsRun} tests passed`)
  }
  if (issuesFound.length > 0) {
    summaryLines.push(`- ${issuesFound.length} issues found`)
  }
  summaryLines.push(`- Total duration: ${totalDuration}ms`)

  return {
    totalSubtasks: results.length,
    succeeded,
    failed,
    filesChanged: [...filesChanged],
    testsRun,
    testsPassed,
    issuesFound,
    summary: summaryLines.join('\n'),
    durationMs: totalDuration,
  }
}
