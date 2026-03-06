/**
 * Command Palette Types & Utilities
 */

import type { JSX } from 'solid-js'

// ============================================================================
// Types
// ============================================================================

export type IconComponent = (props: { class?: string }) => JSX.Element

export interface CommandItem {
  id: string
  label: string
  description?: string
  icon?: IconComponent
  category?: string
  shortcut?: string
  action: () => void
}

export interface CommandPaletteProps {
  /** Available commands */
  commands: CommandItem[]
  /** Called when palette is closed */
  onClose?: () => void
  /** Recent command IDs */
  recentIds?: string[]
}

// ============================================================================
// Fuzzy Matching
// ============================================================================

export const fuzzyMatch = (query: string, text: string): boolean => {
  const queryLower = query.toLowerCase()
  const textLower = text.toLowerCase()

  let queryIdx = 0
  for (let i = 0; i < textLower.length && queryIdx < queryLower.length; i++) {
    if (textLower[i] === queryLower[queryIdx]) {
      queryIdx++
    }
  }

  return queryIdx === queryLower.length
}
