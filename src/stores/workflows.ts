/**
 * Workflows Store
 *
 * Reactive store for workflow/recipe management.
 */

import { createSignal } from 'solid-js'
import { exportAllWorkflows, exportWorkflow, importWorkflowsFromFile } from '../lib/export-workflow'
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

    exportSingle: (id: string) => {
      const w = workflows().find((w) => w.id === id)
      if (w) exportWorkflow(w)
    },

    exportAll: () => {
      exportAllWorkflows(workflows())
    },

    importFromFile: async () => {
      const imported = await importWorkflowsFromFile()
      for (const w of imported) {
        const saved = await saveWorkflowDb({
          projectId: w.projectId,
          name: w.name,
          description: w.description,
          tags: w.tags,
          prompt: w.prompt,
          sourceSessionId: w.sourceSessionId,
        })
        setWorkflows((prev) => [saved, ...prev])
      }
      return imported.length
    },

    /** Set a cron schedule on a workflow */
    scheduleWorkflow: (id: string, cron: string) => {
      setWorkflows((prev) => prev.map((w) => (w.id === id ? { ...w, schedule: cron } : w)))
    },

    /** Remove the cron schedule from a workflow */
    unscheduleWorkflow: (id: string) => {
      setWorkflows((prev) =>
        prev.map((w) => (w.id === id ? { ...w, schedule: undefined, lastRun: undefined } : w))
      )
    },

    /** Get all workflows that have a cron schedule */
    getScheduledWorkflows: () => {
      return workflows().filter((w) => !!w.schedule)
    },

    /** Update the lastRun timestamp for a workflow */
    markWorkflowRun: (id: string) => {
      setWorkflows((prev) => prev.map((w) => (w.id === id ? { ...w, lastRun: Date.now() } : w)))
    },
  }
}
