/**
 * Date Separator Component
 *
 * Visual divider between message groups with "Today", "Yesterday", or date label.
 */

import type { Component } from 'solid-js'

interface DateSeparatorProps {
  label: string
}

export const DateSeparator: Component<DateSeparatorProps> = (props) => (
  <div class="flex items-center gap-3 py-2">
    <div class="flex-1 h-px bg-[var(--border-subtle)]" />
    <span class="text-[10px] font-medium text-[var(--text-muted)] uppercase tracking-wider whitespace-nowrap">
      {props.label}
    </span>
    <div class="flex-1 h-px bg-[var(--border-subtle)]" />
  </div>
)

/**
 * Format a timestamp into a human-readable date label.
 * Returns "Today", "Yesterday", or a formatted date like "Feb 7, 2026".
 */
export function formatDateLabel(timestamp: number): string {
  const date = new Date(timestamp)
  const now = new Date()

  const dateDay = new Date(date.getFullYear(), date.getMonth(), date.getDate())
  const today = new Date(now.getFullYear(), now.getMonth(), now.getDate())
  const diffMs = today.getTime() - dateDay.getTime()
  const diffDays = Math.floor(diffMs / (1000 * 60 * 60 * 24))

  if (diffDays === 0) return 'Today'
  if (diffDays === 1) return 'Yesterday'

  return date.toLocaleDateString('en-US', {
    month: 'short',
    day: 'numeric',
    year: date.getFullYear() !== now.getFullYear() ? 'numeric' : undefined,
  })
}
