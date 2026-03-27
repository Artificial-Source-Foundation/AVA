/**
 * Terminal Helper Utilities
 *
 * ANSI color parsing, status configuration, and formatting helpers
 * for the terminal panel. Extracted from TerminalPanel.tsx.
 */

import { Check, Loader2, X } from 'lucide-solid'
import type { ExecutionStatus } from '../../../types'

// ============================================================================
// ANSI Color Parsing
// ============================================================================

export const ansiToClass: Record<string, string> = {
  '30': 'text-gray-900 dark:text-gray-100',
  '31': 'text-red-600 dark:text-red-400',
  '32': 'text-green-600 dark:text-green-400',
  '33': 'text-yellow-600 dark:text-yellow-400',
  '34': 'text-blue-600 dark:text-blue-400',
  '35': 'text-purple-600 dark:text-purple-400',
  '36': 'text-cyan-600 dark:text-cyan-400',
  '37': 'text-gray-600 dark:text-gray-300',
  '90': 'text-gray-500',
  '91': 'text-red-500',
  '92': 'text-green-500',
  '93': 'text-yellow-500',
  '94': 'text-blue-500',
  '95': 'text-purple-500',
  '96': 'text-cyan-500',
  '97': 'text-white',
}

/** Parse ANSI codes and convert to styled spans */
export const parseAnsi = (text: string): { text: string; class: string }[] => {
  const parts: { text: string; class: string }[] = []
  const ansiPattern = '\\x1b\\[(\\d+)m'
  const regex = new RegExp(ansiPattern, 'g')
  let lastIndex = 0
  let currentClass = ''
  let match: RegExpExecArray | null

  // biome-ignore lint/suspicious/noAssignInExpressions: Standard regex iteration pattern
  while ((match = regex.exec(text)) !== null) {
    // Add text before this match
    if (match.index > lastIndex) {
      parts.push({ text: text.slice(lastIndex, match.index), class: currentClass })
    }

    // Update current class
    const code = match[1]
    if (code === '0') {
      currentClass = '' // Reset
    } else if (ansiToClass[code]) {
      currentClass = ansiToClass[code]
    }

    lastIndex = regex.lastIndex
  }

  // Add remaining text
  if (lastIndex < text.length) {
    parts.push({ text: text.slice(lastIndex), class: currentClass })
  }

  return parts.length > 0 ? parts : [{ text, class: '' }]
}

// ============================================================================
// Status Configuration
// ============================================================================

export const statusConfig: Record<
  ExecutionStatus,
  { color: string; bg: string; icon: typeof Check; label: string }
> = {
  running: { color: 'var(--accent)', bg: 'var(--accent-subtle)', icon: Loader2, label: 'Running' },
  success: { color: 'var(--success)', bg: 'var(--success-subtle)', icon: Check, label: 'Success' },
  error: { color: 'var(--error)', bg: 'var(--error-subtle)', icon: X, label: 'Error' },
}

// ============================================================================
// Formatting Helpers
// ============================================================================

export const formatDuration = (ms: number): string => {
  if (ms < 1000) return `${ms}ms`
  return `${(ms / 1000).toFixed(1)}s`
}

export const formatTimestamp = (ts: number): string => {
  const diff = Date.now() - ts
  if (diff < 60000) return 'Just now'
  if (diff < 3600000) return `${Math.floor(diff / 60000)}m ago`
  return new Date(ts).toLocaleTimeString()
}
