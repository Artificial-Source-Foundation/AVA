/**
 * LSP client — handles protocol lifecycle and operations.
 *
 * Supports: initialize, shutdown, didOpen/didChange/didClose,
 * completion, hover, definition, references, diagnostics.
 */

import type { LSPMessage, LSPTransport } from './transport.js'
import type {
  LSPCodeAction,
  LSPCompletionItem,
  LSPDocumentSymbol,
  LSPHoverResult,
  LSPInitializeResult,
  LSPLocation,
  LSPPosition,
  LSPProtocolDiagnostic,
  LSPPublishDiagnosticsParams,
  LSPRange,
  LSPServerCapabilities,
  LSPWorkspaceEdit,
  LSPWorkspaceSymbol,
} from './types.js'

const DEFAULT_TIMEOUT_MS = 10_000

interface PendingRequest {
  resolve: (result: unknown) => void
  reject: (error: Error) => void
  timer: ReturnType<typeof setTimeout>
}

type DiagnosticsHandler = (params: LSPPublishDiagnosticsParams) => void

export class LSPClient {
  private nextId = 1
  private pending = new Map<number, PendingRequest>()
  private initialized = false
  private capabilities: LSPServerCapabilities = {}
  private timeoutMs: number
  private diagnosticsHandler: DiagnosticsHandler | null = null

  constructor(
    private transport: LSPTransport,
    options?: { timeoutMs?: number }
  ) {
    this.timeoutMs = options?.timeoutMs ?? DEFAULT_TIMEOUT_MS
    this.transport.onMessage((msg) => this.handleMessage(msg))
  }

  // ─── Lifecycle ────────────────────────────────────────────────────────

  async initialize(rootUri: string): Promise<LSPInitializeResult> {
    const result = (await this.request('initialize', {
      processId: null,
      rootUri,
      capabilities: {
        textDocument: {
          synchronization: { dynamicRegistration: false, willSave: false, didSave: true },
          completion: { completionItem: { snippetSupport: false } },
          hover: { contentFormat: ['plaintext', 'markdown'] },
          publishDiagnostics: { relatedInformation: false },
        },
      },
    })) as LSPInitializeResult

    await this.notify('initialized', {})
    this.initialized = true
    this.capabilities = result.capabilities
    return result
  }

  async shutdown(): Promise<void> {
    if (!this.initialized) return
    await this.request('shutdown')
    await this.notify('exit')
    this.initialized = false
  }

  get serverCapabilities(): LSPServerCapabilities {
    return this.capabilities
  }

  // ─── Document Sync ────────────────────────────────────────────────────

  async didOpen(uri: string, languageId: string, text: string, version = 1): Promise<void> {
    this.assertInitialized()
    await this.notify('textDocument/didOpen', {
      textDocument: { uri, languageId, version, text },
    })
  }

  async didChange(uri: string, text: string, version: number): Promise<void> {
    this.assertInitialized()
    await this.notify('textDocument/didChange', {
      textDocument: { uri, version },
      contentChanges: [{ text }],
    })
  }

  async didClose(uri: string): Promise<void> {
    this.assertInitialized()
    await this.notify('textDocument/didClose', {
      textDocument: { uri },
    })
  }

  // ─── Language Features ────────────────────────────────────────────────

  async completion(uri: string, position: LSPPosition): Promise<LSPCompletionItem[]> {
    this.assertInitialized()
    const result = (await this.request('textDocument/completion', {
      textDocument: { uri },
      position,
    })) as { items?: LSPCompletionItem[] } | LSPCompletionItem[] | null

    if (!result) return []
    if (Array.isArray(result)) return result
    return result.items ?? []
  }

  async hover(uri: string, position: LSPPosition): Promise<LSPHoverResult | null> {
    this.assertInitialized()
    const result = (await this.request('textDocument/hover', {
      textDocument: { uri },
      position,
    })) as LSPHoverResult | null
    return result
  }

  async definition(uri: string, position: LSPPosition): Promise<LSPLocation[]> {
    this.assertInitialized()
    const result = (await this.request('textDocument/definition', {
      textDocument: { uri },
      position,
    })) as LSPLocation | LSPLocation[] | null

    if (!result) return []
    if (Array.isArray(result)) return result
    return [result]
  }

  async references(
    uri: string,
    position: LSPPosition,
    includeDeclaration = true
  ): Promise<LSPLocation[]> {
    this.assertInitialized()
    const result = (await this.request('textDocument/references', {
      textDocument: { uri },
      position,
      context: { includeDeclaration },
    })) as LSPLocation[] | null

    return result ?? []
  }

  async documentSymbols(uri: string): Promise<LSPDocumentSymbol[]> {
    this.assertInitialized()
    const result = (await this.request('textDocument/documentSymbol', {
      textDocument: { uri },
    })) as LSPDocumentSymbol[] | null

    return result ?? []
  }

  async workspaceSymbols(query: string): Promise<LSPWorkspaceSymbol[]> {
    this.assertInitialized()
    const result = (await this.request('workspace/symbol', {
      query,
    })) as LSPWorkspaceSymbol[] | null

    return result ?? []
  }

  async codeActions(
    uri: string,
    range: LSPRange,
    diagnostics?: LSPProtocolDiagnostic[]
  ): Promise<LSPCodeAction[]> {
    this.assertInitialized()
    const result = (await this.request('textDocument/codeAction', {
      textDocument: { uri },
      range,
      context: { diagnostics: diagnostics ?? [] },
    })) as LSPCodeAction[] | null

    return result ?? []
  }

  async rename(
    uri: string,
    position: LSPPosition,
    newName: string
  ): Promise<LSPWorkspaceEdit | null> {
    this.assertInitialized()
    const result = (await this.request('textDocument/rename', {
      textDocument: { uri },
      position,
      newName,
    })) as LSPWorkspaceEdit | null

    return result
  }

  // ─── Diagnostics ──────────────────────────────────────────────────────

  onDiagnostics(handler: DiagnosticsHandler): void {
    this.diagnosticsHandler = handler
  }

  // ─── Close ────────────────────────────────────────────────────────────

  async close(): Promise<void> {
    for (const [, pending] of this.pending) {
      clearTimeout(pending.timer)
      pending.reject(new Error('Client closed'))
    }
    this.pending.clear()
    this.initialized = false
    await this.transport.close()
  }

  // ─── Internal ─────────────────────────────────────────────────────────

  private assertInitialized(): void {
    if (!this.initialized) {
      throw new Error('LSP client not initialized. Call initialize() first.')
    }
  }

  private async request(method: string, params?: unknown): Promise<unknown> {
    const id = this.nextId++
    const message: LSPMessage = { jsonrpc: '2.0', id, method, params }

    return new Promise<unknown>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id)
        reject(new Error(`LSP request timed out: ${method} (${this.timeoutMs}ms)`))
      }, this.timeoutMs)

      this.pending.set(id, { resolve, reject, timer })
      this.transport.send(message).catch((err) => {
        clearTimeout(timer)
        this.pending.delete(id)
        reject(err)
      })
    })
  }

  private async notify(method: string, params?: unknown): Promise<void> {
    const message: LSPMessage = { jsonrpc: '2.0', method, params }
    await this.transport.send(message)
  }

  private handleMessage(message: LSPMessage): void {
    // Server notification (has method, no id)
    if (message.method && message.id === undefined) {
      if (message.method === 'textDocument/publishDiagnostics') {
        this.diagnosticsHandler?.(message.params as LSPPublishDiagnosticsParams)
      }
      return
    }

    // Response to our request (has id)
    if (message.id !== undefined && !message.method) {
      const pending = this.pending.get(message.id)
      if (!pending) return

      clearTimeout(pending.timer)
      this.pending.delete(message.id)

      if (message.error) {
        pending.reject(new Error(`LSP error (${message.error.code}): ${message.error.message}`))
      } else {
        pending.resolve(message.result)
      }
    }
  }
}
