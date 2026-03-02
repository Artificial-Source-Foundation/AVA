/**
 * Extended tools extension.
 * Registers 20 additional tools beyond the 6 core tools.
 * Also loads custom user tools from `.ava/tools/` on session open.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { applyPatchTool } from './apply-patch/index.js'
import { bashBackgroundTool } from './bash-background.js'
import { bashKillTool } from './bash-kill.js'
import { bashOutputTool } from './bash-output.js'
import { batchTool } from './batch.js'
import { codesearchTool } from './codesearch.js'
import { completionTool } from './completion.js'
import { createFileTool } from './create.js'
import { loadCustomTools } from './custom-tools.js'
import { deleteFileTool } from './delete.js'
import { lsTool } from './ls.js'
import { multieditTool } from './multiedit.js'
import { planEnterTool, planExitTool } from './plan-mode-tools.js'
import { questionTool } from './question.js'
import { repoMapTool } from './repo-map.js'
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
  codesearchTool,
  repoMapTool,
  planEnterTool,
  planExitTool,
  bashBackgroundTool,
  bashOutputTool,
  bashKillTool,
]

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = TOOLS.map((tool) => api.registerTool(tool))

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
