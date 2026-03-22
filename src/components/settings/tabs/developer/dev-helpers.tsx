/**
 * Developer Tab — helpers and shared components
 *
 * Log formatting helpers, and color/label maps.
 * Toggle is imported from the shared ui component.
 */

import type { Component } from 'solid-js'

export { Toggle } from '../../../ui/Toggle'

// ============================================================================
// Section Header
// ============================================================================

export const DevSectionHeader: Component<{ title: string }> = (props) => (
  <h3 class="text-[var(--settings-text-badge)] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
    {props.title}
  </h3>
)

// ============================================================================
// Log level colors and labels
// ============================================================================

export const levelColor: Record<string, string> = {
  log: 'var(--text-secondary)',
  info: 'var(--accent)',
  warn: '#e5a00d',
  error: 'var(--error)',
}

export const levelLabel: Record<string, string> = {
  log: 'LOG',
  info: 'INF',
  warn: 'WRN',
  error: 'ERR',
}

// ============================================================================
// Formatting helpers
// ============================================================================

export function formatTime(ts: number): string {
  const d = new Date(ts)
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}:${d.getSeconds().toString().padStart(2, '0')}.${d.getMilliseconds().toString().padStart(3, '0')}`
}

export function extractSource(message: string): string {
  const match = /^\[(.*?)\]/.exec(message)
  return match?.[1] ? match[1] : 'unknown'
}
