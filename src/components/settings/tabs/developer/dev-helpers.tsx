/**
 * Developer Tab — helpers and shared components
 *
 * Toggle component, log formatting helpers, and color/label maps.
 */

import type { Component } from 'solid-js'

// ============================================================================
// Toggle
// ============================================================================

export const Toggle: Component<{ checked: boolean; onChange: (v: boolean) => void }> = (props) => (
  <button
    type="button"
    onClick={() => props.onChange(!props.checked)}
    class={`
      relative w-8 h-[18px] rounded-full transition-colors
      ${props.checked ? 'bg-[var(--accent)]' : 'bg-[var(--border-strong)]'}
    `}
  >
    <span
      class="absolute top-[2px] left-[2px] w-[14px] h-[14px] rounded-full bg-white transition-transform"
      style={{
        transform: props.checked ? 'translateX(14px)' : 'translateX(0)',
      }}
    />
  </button>
)

// ============================================================================
// Section Header
// ============================================================================

export const DevSectionHeader: Component<{ title: string }> = (props) => (
  <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
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
