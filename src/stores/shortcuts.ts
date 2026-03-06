/**
 * Centralized Keyboard Shortcuts Store
 *
 * Single global keydown listener. Shortcuts are reactive — editing a
 * binding in settings immediately takes effect. Custom overrides are
 * persisted to localStorage.
 */

import { createSignal } from 'solid-js'
import { STORAGE_KEYS } from '../config/constants'
import { DEFAULT_SHORTCUTS, type ShortcutDef } from './shortcut-defaults'

// Re-export types for consumers
export type { ShortcutDef } from './shortcut-defaults'

export interface ShortcutAction extends ShortcutDef {
  action: () => void
}

// ============================================================================
// Persistence
// ============================================================================

const STORAGE_KEY = STORAGE_KEYS.SHORTCUTS ?? 'ava_shortcuts'

function loadOverrides(): Record<string, string[]> {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (raw) return JSON.parse(raw) as Record<string, string[]>
  } catch {
    /* ignore */
  }
  return {}
}

function saveOverrides(overrides: Record<string, string[]>): void {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(overrides))
  } catch {
    /* ignore */
  }
}

// ============================================================================
// State
// ============================================================================

const [overrides, setOverrides] = createSignal<Record<string, string[]>>(loadOverrides())
const actionMap = new Map<string, () => void>()

// ============================================================================
// Key Matching
// ============================================================================

function normalizeKey(key: string): string {
  const lower = key.toLowerCase()
  if (lower === 'control' || lower === 'meta') return 'ctrl'
  return lower
}

function matchesShortcut(e: KeyboardEvent, keys: string[]): boolean {
  const pressed = new Set<string>()
  if (e.ctrlKey || e.metaKey) pressed.add('ctrl')
  if (e.shiftKey) pressed.add('shift')
  if (e.altKey) pressed.add('alt')

  const actual = normalizeKey(e.key)
  if (actual !== 'ctrl' && actual !== 'shift' && actual !== 'alt') {
    pressed.add(actual)
  }

  if (pressed.size !== keys.length) return false
  return keys.every((k) => pressed.has(k.toLowerCase()))
}

function arraysEqual(a: string[], b: string[]): boolean {
  if (a.length !== b.length) return false
  const sortedA = [...a].sort()
  const sortedB = [...b].sort()
  return sortedA.every((v, i) => v === sortedB[i])
}

// ============================================================================
// Resolved Shortcuts
// ============================================================================

function getResolvedShortcuts(): ShortcutDef[] {
  const ovr = overrides()
  return DEFAULT_SHORTCUTS.map((s) => {
    if (ovr[s.id]) {
      return { ...s, keys: ovr[s.id], isCustom: true }
    }
    return { ...s, isCustom: false }
  })
}

// ============================================================================
// Public API
// ============================================================================

function registerAction(id: string, action: () => void): void {
  actionMap.set(id, action)
}

function updateShortcut(id: string, keys: string[]): void {
  const current = { ...overrides() }
  const def = DEFAULT_SHORTCUTS.find((s) => s.id === id)
  if (def && arraysEqual(def.keys, keys)) {
    delete current[id]
  } else {
    current[id] = keys
  }
  setOverrides(current)
  saveOverrides(current)
}

function resetShortcut(id: string): void {
  const current = { ...overrides() }
  delete current[id]
  setOverrides(current)
  saveOverrides(current)
}

function resetAllShortcuts(): void {
  setOverrides({})
  saveOverrides({})
}

/** IDs of shortcuts allowed in input fields */
const INPUT_ALLOWED_IDS = new Set([
  'command-palette',
  'search-chat',
  'expanded-editor',
  'stash-prompt',
  'restore-prompt',
  'save-checkpoint',
])

function setupShortcutListener(): () => void {
  const handler = (e: KeyboardEvent) => {
    const tag = (e.target as HTMLElement)?.tagName
    const isInput = tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT'

    for (const shortcut of getResolvedShortcuts()) {
      if (matchesShortcut(e, shortcut.keys)) {
        if (isInput && !INPUT_ALLOWED_IDS.has(shortcut.id)) continue

        const action = actionMap.get(shortcut.id)
        if (action) {
          e.preventDefault()
          action()
          return
        }
      }
    }
  }

  document.addEventListener('keydown', handler)
  return () => document.removeEventListener('keydown', handler)
}

// ============================================================================
// Hook
// ============================================================================

export function useShortcuts() {
  return {
    shortcuts: getResolvedShortcuts,
    registerAction,
    updateShortcut,
    resetShortcut,
    resetAll: resetAllShortcuts,
    customCount: () => Object.keys(overrides()).length,
    setupShortcutListener,
  }
}
