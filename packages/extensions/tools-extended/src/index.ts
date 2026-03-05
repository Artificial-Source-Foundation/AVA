/**
 * Extended tools extension.
 * Registers extended tools beyond the core set.
 * Also loads custom user tools from `.ava/tools/` on session open.
 */

import { getSettingsManager } from '@ava/core-v2/config'
import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { applyPatchTool } from './apply-patch/index.js'
import { batchTool } from './batch.js'
import { completionTool } from './completion.js'
import { createFileTool } from './create.js'
import { loadCustomTools } from './custom-tools.js'
import { deleteFileTool } from './delete.js'
import { activate as activateIntegrations } from './integrations/index.js'
import { lsTool } from './ls.js'
import { multieditTool } from './multiedit.js'
import { planEnterTool, planExitTool } from './plan-mode-tools.js'
import { questionTool } from './question.js'
import { taskTool } from './task.js'
import { todoReadTool, todoWriteTool } from './todo.js'
import { webfetchTool } from './webfetch.js'
import { websearchTool } from './websearch.js'

const TOOLS = [
  createFileTool,
  deleteFileTool,
  lsTool,
  completionTool,
  todoReadTool,
  todoWriteTool,
  batchTool,
  questionTool,
  multieditTool,
  taskTool,
  websearchTool,
  webfetchTool,
  applyPatchTool,
  planEnterTool,
  planExitTool,
]

export function activate(api: ExtensionAPI): Disposable {
  getSettingsManager()
  const disposables: Disposable[] = TOOLS.map((tool) => api.registerTool(tool))
  disposables.push(activateIntegrations(api))

  // Load custom user tools when a session opens
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { workingDirectory: string }
      void loadCustomTools(workingDirectory, api).then((customDisposables) => {
        disposables.push(...customDisposables)
        if (customDisposables.length > 0) {
          api.log.info(`Loaded ${customDisposables.length} custom tool(s)`)
        }
      })
    })
  )

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}

export { runEditCascade } from './edit/cascade.js'
export { normalizeForMatch } from './edit/normalize-for-match.js'
export { RelativeIndenter } from './edit/relative-indenter.js'
export { StreamingEditParser } from './edit/streaming-edit-parser.js'
