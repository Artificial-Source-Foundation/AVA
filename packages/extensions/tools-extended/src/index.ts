/**
 * Extended tools extension.
 * Registers 10 additional tools beyond the 6 core tools.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { batchTool } from './batch.js'
import { completionTool } from './completion.js'
import { createFileTool } from './create.js'
import { deleteFileTool } from './delete.js'
import { lsTool } from './ls.js'
import { multieditTool } from './multiedit.js'
import { questionTool } from './question.js'
import { todoReadTool, todoWriteTool } from './todo.js'

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
]

export function activate(api: ExtensionAPI): Disposable {
  const disposables = TOOLS.map((tool) => api.registerTool(tool))

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
