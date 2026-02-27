/**
 * LSP extension — Language Server Protocol integration.
 *
 * Checks for available LSP servers and registers diagnostic event handlers.
 * Actual LSP communication is deferred to future implementation.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { SupportedLanguage } from './types.js'

const LSP_SERVERS: Record<SupportedLanguage, { command: string; args: string[] }> = {
  typescript: { command: 'typescript-language-server', args: ['--stdio'] },
  python: { command: 'pylsp', args: [] },
  rust: { command: 'rust-analyzer', args: [] },
  go: { command: 'gopls', args: ['serve'] },
  java: { command: 'jdtls', args: [] },
}

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []
  const availableLanguages: SupportedLanguage[] = []

  // Check which LSP servers are available
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }

      void (async () => {
        for (const [lang, server] of Object.entries(LSP_SERVERS)) {
          try {
            const result = await api.platform.shell.exec(`which ${server.command}`)
            if (result.exitCode === 0) {
              availableLanguages.push(lang as SupportedLanguage)
            }
          } catch {
            // Server not available
          }
        }

        api.emit('lsp:ready', {
          availableLanguages,
          workingDirectory,
        })

        if (availableLanguages.length > 0) {
          api.log.debug(`LSP servers found: ${availableLanguages.join(', ')}`)
        } else {
          api.log.debug('No LSP servers found')
        }
      })()
    })
  )

  // Handle diagnostic requests
  disposables.push(
    api.on('lsp:diagnostics', (data) => {
      const { file } = data as { file: string }
      // Diagnostic fetching will be implemented when LSP clients are wired
      api.emit('lsp:diagnostics-result', { file, diagnostics: [] })
    })
  )

  api.log.debug('LSP extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
      availableLanguages.length = 0
    },
  }
}
