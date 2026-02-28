/**
 * Workflow Import/Export
 * Serialize workflows to JSON for sharing. Import from file picker.
 */

import type { Workflow } from '../types'

/** Export a single workflow as a JSON download */
export function exportWorkflow(workflow: Workflow): void {
  const data = JSON.stringify(workflow, null, 2)
  const blob = new Blob([data], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `${workflow.name.replace(/[^a-zA-Z0-9-_ ]/g, '')}.workflow.json`
  a.click()
  URL.revokeObjectURL(url)
}

/** Export multiple workflows as a JSON array download */
export function exportAllWorkflows(workflows: Workflow[]): void {
  const data = JSON.stringify(workflows, null, 2)
  const blob = new Blob([data], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `ava-workflows-${new Date().toISOString().slice(0, 10)}.json`
  a.click()
  URL.revokeObjectURL(url)
}

/** Open a file picker and import workflows from a JSON file */
export function importWorkflowsFromFile(): Promise<Workflow[]> {
  return new Promise((resolve, reject) => {
    const input = document.createElement('input')
    input.type = 'file'
    input.accept = '.json'

    input.onchange = async () => {
      const file = input.files?.[0]
      if (!file) return resolve([])

      try {
        const text = await file.text()
        const parsed = JSON.parse(text) as unknown
        const items = Array.isArray(parsed) ? parsed : [parsed]

        const workflows = items.filter(
          (item): item is Workflow =>
            typeof item === 'object' &&
            item !== null &&
            typeof (item as Record<string, unknown>).name === 'string' &&
            typeof (item as Record<string, unknown>).prompt === 'string'
        )

        if (workflows.length === 0) {
          reject(new Error('No valid workflows found in file'))
          return
        }

        resolve(workflows)
      } catch {
        reject(new Error('Failed to parse workflow file'))
      }
    }

    input.click()
  })
}
