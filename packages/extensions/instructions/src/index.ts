/**
 * Instructions extension — loads project/directory instructions.
 *
 * Listens for session:opened events and loads instruction files
 * from the working directory upward. Emits `instructions:loaded` with
 * the merged content so the CLI/app can inject it into the system prompt.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { loadInstructions, mergeInstructions } from './loader.js'
import type { InstructionConfig } from './types.js'
import { DEFAULT_INSTRUCTION_CONFIG } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  let userConfig: Partial<InstructionConfig> = {}
  try {
    userConfig = api.getSettings<Partial<InstructionConfig>>('instructions')
  } catch {
    // Settings category not registered — use defaults
  }
  const config = { ...DEFAULT_INSTRUCTION_CONFIG, ...userConfig }
  const disposables: Disposable[] = []

  disposables.push(
    api.on('session:opened', (data) => {
      const { sessionId, workingDirectory } = data as {
        sessionId: string
        workingDirectory: string
      }

      void loadInstructions(workingDirectory, api.platform.fs, config).then((files) => {
        if (files.length > 0) {
          const merged = mergeInstructions(files)
          void api.storage.set(`instructions:${sessionId}`, files)
          api.emit('instructions:loaded', {
            sessionId,
            files,
            merged,
            count: files.length,
          })
          api.log.info(`Loaded ${files.length} instruction file(s)`)
        }
      })
    })
  )

  api.log.debug('Instructions extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
