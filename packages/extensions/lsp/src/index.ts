/**
 * LSP extension — Language Server Protocol integration.
 *
 * Starts available LSP servers and registers diagnostic/hover tools.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { formatDiagnostics, formatHover, formatLocations } from './queries.js'
import { LSPServerManager, pathToUri } from './server-manager.js'
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

  // Register lsp_diagnostics tool
  disposables.push(
    api.registerTool({
      definition: {
        name: 'lsp_diagnostics',
        description: 'Get LSP diagnostics (errors, warnings) for a file.',
        input_schema: {
          type: 'object' as const,
          properties: {
            file: { type: 'string', description: 'File path to get diagnostics for' },
          },
          required: ['file'],
        },
      },
      async execute(input) {
        const { file } = input as { file: string }
        const diags = diagnosticsStore.get(file) ?? []
        return { success: true, output: formatDiagnostics(diags) }
      },
    })
  )

  // Register lsp_hover tool
  disposables.push(
    api.registerTool({
      definition: {
        name: 'lsp_hover',
        description: 'Get hover information (type info, docs) for a symbol at a position.',
        input_schema: {
          type: 'object' as const,
          properties: {
            file: { type: 'string', description: 'File path' },
            line: { type: 'number', description: 'Line number (1-based)' },
            column: { type: 'number', description: 'Column number (1-based)' },
            language: {
              type: 'string',
              description: 'Language: typescript, python, rust, go, java',
            },
          },
          required: ['file', 'line', 'column', 'language'],
        },
      },
      async execute(input) {
        const { file, line, column, language } = input as {
          file: string
          line: number
          column: number
          language: SupportedLanguage
        }

        if (!serverManager) {
          return { success: false, output: 'LSP not initialized. Open a session first.' }
        }

        const client = serverManager.getClient(language)
        if (!client) {
          return { success: false, output: `No LSP server running for ${language}` }
        }

        try {
          const uri = pathToUri(file)
          const hover = await client.hover(uri, { line: line - 1, character: column - 1 })
          return { success: true, output: formatHover(hover) }
        } catch (err) {
          return { success: false, output: `LSP hover failed: ${err}` }
        }
      },
    })
  )

  // Register lsp_definition tool
  disposables.push(
    api.registerTool({
      definition: {
        name: 'lsp_definition',
        description: 'Go to definition of a symbol at a position.',
        input_schema: {
          type: 'object' as const,
          properties: {
            file: { type: 'string', description: 'File path' },
            line: { type: 'number', description: 'Line number (1-based)' },
            column: { type: 'number', description: 'Column number (1-based)' },
            language: {
              type: 'string',
              description: 'Language: typescript, python, rust, go, java',
            },
          },
          required: ['file', 'line', 'column', 'language'],
        },
      },
      async execute(input) {
        const { file, line, column, language } = input as {
          file: string
          line: number
          column: number
          language: SupportedLanguage
        }

        if (!serverManager) {
          return { success: false, output: 'LSP not initialized. Open a session first.' }
        }

        const client = serverManager.getClient(language)
        if (!client) {
          return { success: false, output: `No LSP server running for ${language}` }
        }

        try {
          const uri = pathToUri(file)
          const locations = await client.definition(uri, { line: line - 1, character: column - 1 })
          return { success: true, output: formatLocations(locations) }
        } catch (err) {
          return { success: false, output: `LSP definition failed: ${err}` }
        }
      },
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
