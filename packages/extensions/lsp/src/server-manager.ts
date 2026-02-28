/**
 * LSP server manager — manages per-language server lifecycle.
 */

import type { IShell } from '@ava/core-v2/platform'
import { LSPClient } from './client.js'
import { LSPTransport } from './transport.js'
import type { LSPDiagnostic, LSPPublishDiagnosticsParams, SupportedLanguage } from './types.js'

const SEVERITY_MAP: Record<number, LSPDiagnostic['severity']> = {
  1: 'error',
  2: 'warning',
  3: 'info',
  4: 'hint',
}

interface ActiveServer {
  language: SupportedLanguage
  client: LSPClient
  transport: LSPTransport
  rootUri: string
}

export class LSPServerManager {
  private servers = new Map<SupportedLanguage, ActiveServer>()
  private diagnosticsCallback: ((diagnostics: LSPDiagnostic[]) => void) | null = null

  constructor(private shell: IShell) {}

  async startServer(
    language: SupportedLanguage,
    command: string,
    args: string[],
    rootUri: string
  ): Promise<void> {
    // Stop existing server for this language
    if (this.servers.has(language)) {
      await this.stopServer(language)
    }

    const transport = new LSPTransport(this.shell, command, args)
    const client = new LSPClient(transport)

    await transport.start()
    await client.initialize(rootUri)

    // Wire up diagnostics
    client.onDiagnostics((params) => {
      this.handleDiagnostics(params)
    })

    // Reconnect on close
    transport.onClose(() => {
      this.servers.delete(language)
    })

    this.servers.set(language, { language, client, transport, rootUri })
  }

  async stopServer(language: SupportedLanguage): Promise<void> {
    const server = this.servers.get(language)
    if (!server) return

    try {
      await server.client.shutdown()
    } catch {
      // Ignore shutdown errors
    }
    try {
      await server.client.close()
    } catch {
      // Ignore close errors
    }
    this.servers.delete(language)
  }

  getClient(language: SupportedLanguage): LSPClient | null {
    return this.servers.get(language)?.client ?? null
  }

  getActiveLanguages(): SupportedLanguage[] {
    return [...this.servers.keys()]
  }

  onDiagnostics(callback: (diagnostics: LSPDiagnostic[]) => void): void {
    this.diagnosticsCallback = callback
  }

  private handleDiagnostics(params: LSPPublishDiagnosticsParams): void {
    const file = uriToPath(params.uri)
    const diagnostics: LSPDiagnostic[] = params.diagnostics.map((d) => ({
      file,
      line: d.range.start.line + 1,
      column: d.range.start.character + 1,
      severity: SEVERITY_MAP[d.severity ?? 1] ?? 'error',
      message: d.message,
      source: d.source,
    }))

    this.diagnosticsCallback?.(diagnostics)
  }

  async stopAll(): Promise<void> {
    const languages = [...this.servers.keys()]
    for (const lang of languages) {
      await this.stopServer(lang)
    }
  }
}

/** Convert file:// URI to file path. */
export function uriToPath(uri: string): string {
  if (uri.startsWith('file://')) {
    return decodeURIComponent(uri.slice(7))
  }
  return uri
}

/** Convert file path to file:// URI. */
export function pathToUri(filePath: string): string {
  return `file://${encodeURI(filePath)}`
}
