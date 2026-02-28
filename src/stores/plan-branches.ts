/**
 * Plan Branch Management Store
 *
 * Allows users to create, switch, compare, and merge
 * conversation branches during plan mode exploration.
 */

import { createMemo, createSignal } from 'solid-js'
import type { Message } from '../types'

// ─── Types ──────────────────────────────────────────────────────────────────

export interface PlanBranch {
  id: string
  name: string
  messages: Message[]
  createdAt: number
}

export interface BranchDiff {
  onlyInA: Message[]
  onlyInB: Message[]
  shared: Message[]
}

// ─── State ──────────────────────────────────────────────────────────────────

const [branches, setBranches] = createSignal<PlanBranch[]>([])
const [activeBranchId, setActiveBranchId] = createSignal<string | null>(null)

// ─── Helpers ────────────────────────────────────────────────────────────────

function generateId(): string {
  return `branch-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`
}

// ─── Store ──────────────────────────────────────────────────────────────────

let storeInstance: ReturnType<typeof createPlanBranchesStore> | null = null

function createPlanBranchesStore() {
  const activeBranch = createMemo(() => {
    const id = activeBranchId()
    if (!id) return null
    return branches().find((b) => b.id === id) ?? null
  })

  return {
    /** All plan branches */
    branches,

    /** Currently active branch ID */
    activeBranchId,

    /** Currently active branch (derived) */
    activeBranch,

    /**
     * Create a new branch from current messages.
     * Returns the new branch and sets it as active.
     */
    createBranch(name: string, currentMessages: Message[]): PlanBranch {
      const branch: PlanBranch = {
        id: generateId(),
        name,
        messages: [...currentMessages],
        createdAt: Date.now(),
      }
      setBranches((prev) => [...prev, branch])
      setActiveBranchId(branch.id)
      return branch
    },

    /**
     * Switch to a different branch.
     * Returns the branch's messages so the caller can apply them.
     */
    switchBranch(id: string): Message[] | null {
      const branch = branches().find((b) => b.id === id)
      if (!branch) return null
      setActiveBranchId(id)
      return [...branch.messages]
    },

    /**
     * Delete a branch. If it's the active branch, deactivate.
     */
    deleteBranch(id: string): void {
      setBranches((prev) => prev.filter((b) => b.id !== id))
      if (activeBranchId() === id) {
        setActiveBranchId(null)
      }
    },

    /**
     * Compare two branches by message content.
     * Returns messages unique to each branch and shared messages.
     */
    compareBranches(aId: string, bId: string): BranchDiff | null {
      const a = branches().find((b) => b.id === aId)
      const b = branches().find((br) => br.id === bId)
      if (!a || !b) return null

      const aIds = new Set(a.messages.map((m) => m.id))
      const bIds = new Set(b.messages.map((m) => m.id))

      return {
        onlyInA: a.messages.filter((m) => !bIds.has(m.id)),
        onlyInB: b.messages.filter((m) => !aIds.has(m.id)),
        shared: a.messages.filter((m) => bIds.has(m.id)),
      }
    },

    /**
     * Merge source branch messages into the current message list.
     * Appends any messages from source that aren't already present.
     * Returns the merged message array.
     */
    mergeBranch(sourceId: string, currentMessages: Message[]): Message[] | null {
      const source = branches().find((b) => b.id === sourceId)
      if (!source) return null

      const existingIds = new Set(currentMessages.map((m) => m.id))
      const newMessages = source.messages.filter((m) => !existingIds.has(m.id))
      return [...currentMessages, ...newMessages]
    },

    /**
     * Update the stored messages for a branch (save current state).
     */
    updateBranchMessages(id: string, messages: Message[]): void {
      setBranches((prev) => prev.map((b) => (b.id === id ? { ...b, messages: [...messages] } : b)))
    },

    /**
     * Clear all branches (e.g., when leaving plan mode).
     */
    clearBranches(): void {
      setBranches([])
      setActiveBranchId(null)
    },
  }
}

/**
 * Singleton hook for plan branch management.
 */
export function usePlanBranches() {
  if (!storeInstance) {
    storeInstance = createPlanBranchesStore()
  }
  return storeInstance
}

/** Reset singleton (for testing) */
export function resetPlanBranchesStore(): void {
  storeInstance = null
  setBranches([])
  setActiveBranchId(null)
}
