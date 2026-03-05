/**
 * Extended tools extension.
 * Registers 20 additional tools beyond the 6 core tools.
 * Also loads custom user tools from `.ava/tools/` on session open.
 */

import { getSettingsManager } from '@ava/core-v2/config'
import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { applyPatchTool } from './apply-patch/index.js'
import { bashBackgroundTool } from './bash-background.js'
import { bashKillTool } from './bash-kill.js'
import { bashOutputTool } from './bash-output.js'
import { batchTool } from './batch.js'
import { codesearchTool } from './codesearch.js'
import { completionTool } from './completion.js'
import { createFileTool } from './create.js'
import { createRuleTool } from './create-rule.js'
import { createSkillTool } from './create-skill.js'
import { loadCustomTools } from './custom-tools.js'
import { deleteFileTool } from './delete.js'
import { editBenchmarkTool } from './edit-benchmark/index.js'
import { inlineSuggestTool } from './inline-suggest.js'
import { activate as activateIntegrations } from './integrations/index.js'
import { lsTool } from './ls.js'
import { multieditTool } from './multiedit.js'
import { planEnterTool, planExitTool } from './plan-mode-tools.js'
import { questionTool } from './question.js'
import { repoMapTool } from './repo-map.js'
import { sessionCostTool } from './session-cost.js'
import { taskTool } from './task.js'
import { todoReadTool, todoWriteTool } from './todo.js'
import { viewImageTool } from './view-image.js'
import { isVisionCapable } from './vision-capability.js'
import { DEFAULT_VOICE_SETTINGS } from './voice-settings.js'
import { voiceTranscribeTool } from './voice-transcribe.js'
import { webfetchTool } from './webfetch.js'
import { websearchTool } from './websearch.js'

const TOOLS = [
  createFileTool,
  deleteFileTool,
  lsTool,
  completionTool,
  inlineSuggestTool,
  todoReadTool,
  todoWriteTool,
  batchTool,
  questionTool,
  multieditTool,
  taskTool,
  viewImageTool,
  voiceTranscribeTool,
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
  createRuleTool,
  createSkillTool,
  editBenchmarkTool,
  sessionCostTool,
]

export function activate(api: ExtensionAPI): Disposable {
  getSettingsManager().registerCategory('voice', DEFAULT_VOICE_SETTINGS)
  const disposables: Disposable[] = TOOLS.map((tool) => api.registerTool(tool))
  disposables.push(activateIntegrations(api))

  // Vision capability guard — strip ImageBlock from tool results when the
  // current model doesn't support vision, preventing provider API errors.
  disposables.push(
    api.addToolMiddleware({
      name: 'vision-capability-guard',
      priority: 90, // Run late, after tool execution
      async after(context, result) {
        if (context.toolName !== 'view_image') return undefined
        if (!result.success || !result.metadata) return undefined

        const image = (result.metadata as Record<string, unknown>).image
        if (!image) return undefined

        // Check if the current model supports vision
        const model = (context.ctx as unknown as Record<string, unknown>).model as
          | string
          | undefined
        if (model && !isVisionCapable(model)) {
          return {
            result: {
              success: true,
              output: `${result.output}\n\nNote: current model (${model}) does not support vision. Image data was stripped from the response.`,
              metadata: { visionStripped: true, originalModel: model },
            },
          }
        }

        return undefined
      },
    })
  )

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
