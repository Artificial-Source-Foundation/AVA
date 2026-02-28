/**
 * Workflows Store
 *
 * Reactive store for workflow/recipe management.
 */

import { createSignal } from 'solid-js'
import {
  createWorkflowFromSession,
  deleteWorkflow as deleteWorkflowDb,
  getWorkflows,
  incrementUsageCount,
  saveWorkflow as saveWorkflowDb,
} from '../services/workflows'
import type { Workflow } from '../types'

const [workflows, setWorkflows] = createSignal<Workflow[]>([])

export function useWorkflows() {
  return {
    workflows,

    loadWorkflows: async (projectId?: string) => {
      const list = await getWorkflows(projectId)
      setWorkflows(list)
    },

    saveWorkflow: async (w: Omit<Workflow, 'id' | 'createdAt' | 'updatedAt' | 'usageCount'>) => {
      const saved = await saveWorkflowDb(w)
      setWorkflows((prev) => [saved, ...prev])
      return saved
    },

    deleteWorkflow: async (id: string) => {
      await deleteWorkflowDb(id)
      setWorkflows((prev) => prev.filter((w) => w.id !== id))
    },

    applyWorkflow: (workflow: Workflow) => {
      incrementUsageCount(workflow.id)
      setWorkflows((prev) =>
        prev
          .map((w) => (w.id === workflow.id ? { ...w, usageCount: w.usageCount + 1 } : w))
          .sort((a, b) => b.usageCount - a.usageCount)
      )
      window.dispatchEvent(new CustomEvent('ava:set-input', { detail: { text: workflow.prompt } }))
    },

    createFromSession: async (
      sessionId: string,
      name: string,
      description: string,
      tags: string[],
      projectId?: string
    ) => {
      const workflow = await createWorkflowFromSession(
        sessionId,
        name,
        description,
        tags,
        projectId
      )
      setWorkflows((prev) => [workflow, ...prev])
      return workflow
    },
  }
}
