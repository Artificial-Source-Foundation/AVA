/**
 * Instructions extension — loads project/directory instructions.
 *
 * Listens for session:opened events and loads instruction files
 * from the working directory upward.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { loadInstructions, mergeInstructions } from './loader.js'
import type { InstructionConfig } from './types.js'
import { DEFAULT_INSTRUCTION_CONFIG } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  const config = {
    ...DEFAULT_INSTRUCTION_CONFIG,
    ...api.getSettings<Partial<InstructionConfig>>('instructions'),
  }
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
          api.log.debug(`Loaded ${files.length} instruction file(s)`)
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
