/**
 * Agent Tool Activity
 * Manages tool activity lifecycle for the agent hook
 */

import type { Setter } from 'solid-js'
import type { ToolCallInfo } from './agent-events'
import type { ToolActivity } from './agent-types'

/** Add a new tool activity entry */
export function addToolActivity(setter: Setter<ToolActivity[]>, activity: ToolActivity): void {
  setter((prev) => [...prev, activity])
}

/** Update a running tool activity by name */
export function updateToolActivity(
  setter: Setter<ToolActivity[]>,
  toolName: string,
  updates: Partial<ToolActivity>
): void {
  setter((prev) =>
    prev.map((a) => (a.name === toolName && a.status === 'running' ? { ...a, ...updates } : a))
  )
}

/** Sync final tool results from turn finish */
export function updateToolActivityBatch(
  setter: Setter<ToolActivity[]>,
  toolCalls: ToolCallInfo[]
): void {
  setter((prev) => {
    const updated = [...prev]
    for (const call of toolCalls) {
      const idx = updated.findIndex((a) => a.name === call.name && a.status === 'running')
      if (idx !== -1) {
        updated[idx] = {
          ...updated[idx],
          status: call.success ? 'success' : 'error',
          output: call.result,
          durationMs: call.durationMs,
          completedAt: Date.now(),
        }
      }
    }
    return updated
  })
}
