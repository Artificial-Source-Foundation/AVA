/**
 * Custom commands extension — TOML-based user commands.
 *
 * Discovers and registers user-defined commands from TOML files
 * in configured search paths.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { parseCommandFile } from './parser.js'
import type { CustomCommandConfig } from './types.js'
import { DEFAULT_CUSTOM_COMMAND_CONFIG } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  const config = {
    ...DEFAULT_CUSTOM_COMMAND_CONFIG,
    ...api.getSettings<Partial<CustomCommandConfig>>('custom-commands'),
  }
  const disposables: Disposable[] = []

  // Scan for command files on session open
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }

      void (async () => {
        let totalRegistered = 0

        for (const searchPath of config.searchPaths) {
          // Resolve ~ to home directory and relative paths
          const resolvedPath = searchPath.startsWith('~')
            ? searchPath // Let the platform resolve ~
            : `${workingDirectory}/${searchPath}`

          for (const pattern of config.fileNames) {
            try {
              const files = await api.platform.fs.glob(pattern, resolvedPath)
              for (const file of files) {
                try {
                  const content = await api.platform.fs.readFile(file)
                  const cmd = parseCommandFile(content, file)
                  if (cmd) {
                    disposables.push(
                      api.registerCommand({
                        name: cmd.name,
                        description: cmd.description,
                        async execute() {
                          return cmd.prompt
                        },
                      })
                    )
                    totalRegistered++
                  }
                } catch {
                  // Skip unreadable files
                }
              }
            } catch {
              // Search path doesn't exist — skip
            }
          }
        }

        if (totalRegistered > 0) {
          api.log.debug(`Loaded ${totalRegistered} custom command(s)`)
          api.emit('custom-commands:loaded', { count: totalRegistered })
        }
      })()
    })
  )

  api.log.debug('Custom commands extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
