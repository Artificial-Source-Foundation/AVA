/**
 * Commander Utilities
 * Helper functions for phone book generation, result formatting, and error aggregation
 */

import type { WorkerRegistry } from './registry.js'
import type { CombinedWorkerResult, WorkerResult } from './types.js'

// ============================================================================
// Phone Book Generation
// ============================================================================

/**
 * Generate the phone book context for a commander's system prompt
 *
 * This is a convenience wrapper around WorkerRegistry.getDirectoryContext()
 *
 * @param registry - Worker registry to generate phone book from
 * @returns Formatted phone book string
 */
export function generatePhoneBook(registry: WorkerRegistry): string {
  return registry.getDirectoryContext()
}

/**
 * Generate a compact phone book (just worker names and descriptions)
 *
 * Useful for prompts with limited context
 *
 * @param registry - Worker registry
 * @returns Compact phone book string
 */
export function generateCompactPhoneBook(registry: WorkerRegistry): string {
  const workers = registry.getAllWorkers()
  if (workers.length === 0) {
    return 'No workers available.'
  }

  return workers.map((w) => `- **delegate_${w.name}**: ${w.description.split('.')[0]}.`).join('\n')
}

// ============================================================================
// Output Formatting
// ============================================================================

/**
 * Format a single worker's output for display
 *
 * @param workerName - Name of the worker
 * @param result - Worker result
 * @returns Formatted output string
 */
export function formatWorkerOutput(workerName: string, result: WorkerResult): string {
  const lines: string[] = []

  // Header
  const status = result.success ? 'SUCCESS' : 'FAILED'
  lines.push(`## Worker: ${workerName} [${status}]`)
  lines.push('')

  // Output
  if (result.output) {
    lines.push(result.output)
    lines.push('')
  }

  // Error details if failed
  if (!result.success && result.error) {
    lines.push(`**Error:** ${result.error}`)
    lines.push('')
  }

  // Stats
  lines.push('---')
  lines.push(
    `*Turns: ${result.turns} | Tokens: ${result.tokensUsed} | Duration: ${result.durationMs}ms*`
  )

  return lines.join('\n')
}

/**
 * Format worker output as a summary (brief version)
 */
export function formatWorkerSummary(workerName: string, result: WorkerResult): string {
  const status = result.success ? 'completed' : 'failed'
  const brief = result.output?.slice(0, 100) ?? 'No output'
  return `- **${workerName}**: ${status} (${brief}${brief.length >= 100 ? '...' : ''})`
}

// ============================================================================
// Result Aggregation
// ============================================================================

/**
 * Combined result entry from a single worker
 */
interface WorkerResultEntry {
  worker: string
  result: WorkerResult
}

/**
 * Combine results from multiple workers into a single result
 *
 * @param results - Array of worker results with worker names
 * @returns Combined result
 */
export function combineWorkerResults(results: WorkerResultEntry[]): CombinedWorkerResult {
  if (results.length === 0) {
    return {
      success: true,
      summary: 'No workers were executed.',
      details: '',
      results: [],
      totalTokensUsed: 0,
      totalDurationMs: 0,
    }
  }

  // Calculate aggregates
  const allSuccess = results.every((r) => r.result.success)
  const totalTokens = results.reduce((sum, r) => sum + r.result.tokensUsed, 0)
  const totalDuration = results.reduce((sum, r) => sum + r.result.durationMs, 0)

  // Generate summary
  const successCount = results.filter((r) => r.result.success).length
  const failCount = results.length - successCount
  const summary = generateCombinedSummary(results, successCount, failCount)

  // Generate detailed output
  const details = results.map((r) => formatWorkerOutput(r.worker, r.result)).join('\n\n')

  return {
    success: allSuccess,
    summary,
    details,
    results,
    totalTokensUsed: totalTokens,
    totalDurationMs: totalDuration,
  }
}

/**
 * Generate a summary of combined worker results
 */
function generateCombinedSummary(
  results: WorkerResultEntry[],
  successCount: number,
  failCount: number
): string {
  const lines: string[] = []

  // Status line
  if (failCount === 0) {
    lines.push(`All ${successCount} worker(s) completed successfully.`)
  } else if (successCount === 0) {
    lines.push(`All ${failCount} worker(s) failed.`)
  } else {
    lines.push(`${successCount} worker(s) succeeded, ${failCount} failed.`)
  }

  // Per-worker summaries
  lines.push('')
  for (const entry of results) {
    lines.push(formatWorkerSummary(entry.worker, entry.result))
  }

  return lines.join('\n')
}

// ============================================================================
// Error Handling
// ============================================================================

/**
 * Aggregate errors from multiple worker results
 *
 * @param results - Array of worker results
 * @returns Array of error messages
 */
export function aggregateErrors(results: WorkerResultEntry[]): string[] {
  return results
    .filter((r) => !r.result.success && r.result.error)
    .map((r) => `[${r.worker}] ${r.result.error}`)
}

/**
 * Format aggregated errors for display
 *
 * @param results - Array of worker results
 * @returns Formatted error string or null if no errors
 */
export function formatAggregatedErrors(results: WorkerResultEntry[]): string | null {
  const errors = aggregateErrors(results)
  if (errors.length === 0) {
    return null
  }

  const lines = ['## Worker Errors', '', ...errors.map((e) => `- ${e}`)]
  return lines.join('\n')
}

/**
 * Check if any workers failed
 */
export function hasWorkerFailures(results: WorkerResultEntry[]): boolean {
  return results.some((r) => !r.result.success)
}

/**
 * Get failed worker names
 */
export function getFailedWorkers(results: WorkerResultEntry[]): string[] {
  return results.filter((r) => !r.result.success).map((r) => r.worker)
}

// ============================================================================
// Statistics
// ============================================================================

/**
 * Calculate statistics from worker results
 */
export function calculateWorkerStats(results: WorkerResultEntry[]): {
  totalWorkers: number
  successfulWorkers: number
  failedWorkers: number
  totalTokens: number
  totalDurationMs: number
  averageTokensPerWorker: number
  averageDurationPerWorker: number
} {
  const total = results.length
  const successful = results.filter((r) => r.result.success).length
  const failed = total - successful
  const totalTokens = results.reduce((sum, r) => sum + r.result.tokensUsed, 0)
  const totalDuration = results.reduce((sum, r) => sum + r.result.durationMs, 0)

  return {
    totalWorkers: total,
    successfulWorkers: successful,
    failedWorkers: failed,
    totalTokens,
    totalDurationMs: totalDuration,
    averageTokensPerWorker: total > 0 ? Math.round(totalTokens / total) : 0,
    averageDurationPerWorker: total > 0 ? Math.round(totalDuration / total) : 0,
  }
}
