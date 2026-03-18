/**
 * Time Formatting Utilities
 *
 * Consolidated from duplicate implementations across the codebase.
 */

/** Format a duration in milliseconds: "450ms", "1.2s", "2m 30s" */
export function formatMs(ms: number): string {
  if (ms < 1000) return `${ms}ms`
  if (ms < 60_000) return `${(ms / 1000).toFixed(1)}s`
  const mins = Math.floor(ms / 60_000)
  const secs = Math.round((ms % 60_000) / 1000)
  return `${mins}m ${secs}s`
}

/** Format live elapsed time from a start timestamp (Date.now()-based): "<1s", "3.2s" */
export function formatElapsedSince(startedAt: number): string {
  const elapsed = Date.now() - startedAt
  if (elapsed < 1000) return '<1s'
  return `${(elapsed / 1000).toFixed(1)}s`
}

/** Format a duration in whole seconds: "33s", "2m 5s" */
export function formatSeconds(sec: number): string {
  if (sec < 60) return `${sec}s`
  const m = Math.floor(sec / 60)
  const s = sec % 60
  return `${m}m${s > 0 ? ` ${s}s` : ''}`
}
