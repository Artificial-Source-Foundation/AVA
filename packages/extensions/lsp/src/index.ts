/**
 * LSP extension — Language Server Protocol integration.
 *
 * Starts available LSP servers and registers 9 tools:
 * diagnostics, hover, definition, references, document_symbols,
 * workspace_symbols, code_actions, rename, completions.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { LSPServerManager, pathToUri } from './server-manager.js'
import { createAllLspTools } from './tools.js'
import type { LSPDiagnostic, SupportedLanguage } from './types.js'

const LSP_SERVERS: Record<SupportedLanguage, { command: string; args: string[] }> = {
  typescript: { command: 'typescript-language-server', args: ['--stdio'] },
  python: { command: 'pylsp', args: [] },
  rust: { command: 'rust-analyzer', args: [] },
  go: { command: 'gopls', args: ['serve'] },
  java: { command: 'jdtls', args: [] },
}

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []
  let serverManager: LSPServerManager | null = null
  const diagnosticsStore = new Map<string, LSPDiagnostic[]>()

  // Register all 9 LSP tools
  const tools = createAllLspTools({
    getServerManager: () => serverManager,
    getDiagnosticsStore: () => diagnosticsStore,
  })
  for (const tool of tools) {
    disposables.push(api.registerTool(tool))
  }

  // Start available servers on session open
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }
      serverManager = new LSPServerManager(api.platform.shell)

      // Collect diagnostics
      serverManager.onDiagnostics((diagnostics) => {
        if (diagnostics.length > 0) {
          const file = diagnostics[0]!.file
          diagnosticsStore.set(file, diagnostics)
        }
        api.emit('lsp:diagnostics-update', { diagnostics })
      })

      void (async () => {
        const availableLanguages: SupportedLanguage[] = []

        for (const [lang, server] of Object.entries(LSP_SERVERS)) {
          try {
            const result = await api.platform.shell.exec(`which ${server.command}`)
            if (result.exitCode === 0) {
              availableLanguages.push(lang as SupportedLanguage)
              try {
                const rootUri = pathToUri(workingDirectory)
                await serverManager!.startServer(
                  lang as SupportedLanguage,
                  server.command,
                  server.args,
                  rootUri
                )
                api.log.debug(`LSP server started: ${lang}`)
              } catch (err) {
                api.log.warn(`LSP server failed to start (${lang}): ${err}`)
              }
            }
          } catch {
            // Server not available
          }
        }

        api.emit('lsp:ready', {
          availableLanguages,
          activeLanguages: serverManager!.getActiveLanguages(),
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

  api.log.debug('LSP extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
      if (serverManager) {
        serverManager.stopAll().catch(() => {})
        serverManager = null
      }
      diagnosticsStore.clear()
    },
  }
}
