/**
 * Sandbox Store
 *
 * Accumulates file changes during "sandbox mode" instead of writing to disk.
 * Users can review, selectively accept, or reject changes via the SandboxReviewDialog.
 */

import { isTauri } from '@tauri-apps/api/core'
import { createMemo, createSignal } from 'solid-js'
import { logInfo, logWarn } from '../services/logger'

// ============================================================================
// Types
// ============================================================================

export interface PendingChange {
  filePath: string
  originalContent: string
  newContent: string
  type: 'create' | 'modify' | 'delete'
  timestamp: number
}

// ============================================================================
// Store
// ============================================================================

let store: ReturnType<typeof createSandboxStore> | null = null

export function resetSandboxStore() {
  store = null
}

function createSandboxStore() {
  const [sandboxEnabled, setSandboxEnabled] = createSignal(false)
  const [pendingChanges, setPendingChanges] = createSignal<PendingChange[]>([])
  const [reviewDialogOpen, setReviewDialogOpen] = createSignal(false)

  const pendingCount = createMemo(() => pendingChanges().length)

  const pendingByType = createMemo(() => {
    const changes = pendingChanges()
    return {
      created: changes.filter((c) => c.type === 'create').length,
      modified: changes.filter((c) => c.type === 'modify').length,
      deleted: changes.filter((c) => c.type === 'delete').length,
    }
  })

  /** Add a pending change. If a change for the same file already exists, replace it. */
  function addPendingChange(change: Omit<PendingChange, 'timestamp'>): void {
    setPendingChanges((prev) => {
      const filtered = prev.filter((c) => c.filePath !== change.filePath)
      return [...filtered, { ...change, timestamp: Date.now() }]
    })
    logInfo('sandbox', `Queued ${change.type}: ${change.filePath}`)
  }

  /** Discard all pending changes */
  function clearPendingChanges(): void {
    setPendingChanges([])
    logInfo('sandbox', 'Cleared all pending changes')
  }

  /** Apply selected changes to disk using Tauri FS */
  async function applySelectedChanges(
    selectedPaths: string[]
  ): Promise<{ applied: number; failed: string[] }> {
    const changes = pendingChanges()
    const toApply = changes.filter((c) => selectedPaths.includes(c.filePath))
    const failed: string[] = []
    let applied = 0

    if (!isTauri()) {
      logWarn('sandbox', 'Cannot write files outside Tauri runtime')
      return { applied: 0, failed: selectedPaths }
    }

    const fs = await import('@tauri-apps/plugin-fs')

    for (const change of toApply) {
      try {
        if (change.type === 'delete') {
          await fs.remove(change.filePath)
        } else {
          // Ensure parent directory exists
          const parentDir = change.filePath.substring(0, change.filePath.lastIndexOf('/'))
          if (parentDir) {
            try {
              await fs.mkdir(parentDir, { recursive: true })
            } catch {
              // Already exists
            }
          }
          await fs.writeTextFile(change.filePath, change.newContent)
        }
        applied++
        logInfo('sandbox', `Applied ${change.type}: ${change.filePath}`)
      } catch (err) {
        failed.push(change.filePath)
        logWarn('sandbox', `Failed to apply ${change.filePath}`, err)
      }
    }

    // Remove applied changes from the pending list
    setPendingChanges((prev) =>
      prev.filter((c) => !selectedPaths.includes(c.filePath) || failed.includes(c.filePath))
    )

    return { applied, failed }
  }

  /** Apply all pending changes to disk */
  async function applyAllChanges(): Promise<{ applied: number; failed: string[] }> {
    const paths = pendingChanges().map((c) => c.filePath)
    return applySelectedChanges(paths)
  }

  /** Reject all changes (alias for clear) */
  function rejectAllChanges(): void {
    clearPendingChanges()
  }

  /** Toggle sandbox mode on/off */
  function toggleSandbox(): void {
    const newValue = !sandboxEnabled()
    setSandboxEnabled(newValue)
    logInfo('sandbox', `Sandbox mode ${newValue ? 'enabled' : 'disabled'}`)
  }

  /** Open/close the review dialog */
  function openReview(): void {
    setReviewDialogOpen(true)
  }

  function closeReview(): void {
    setReviewDialogOpen(false)
  }

  return {
    sandboxEnabled,
    setSandboxEnabled,
    pendingChanges,
    pendingCount,
    pendingByType,
    reviewDialogOpen,
    addPendingChange,
    clearPendingChanges,
    applySelectedChanges,
    applyAllChanges,
    rejectAllChanges,
    toggleSandbox,
    openReview,
    closeReview,
  }
}

export function useSandbox() {
  if (!store) store = createSandboxStore()
  return store
}
