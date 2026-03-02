/**
 * LSP extension types.
 */

export type SupportedLanguage = 'typescript' | 'python' | 'rust' | 'go' | 'java'

export interface LSPDiagnostic {
  file: string
  line: number
  column: number
  severity: 'error' | 'warning' | 'info' | 'hint'
  message: string
  source?: string
}

export interface LSPServer {
  language: SupportedLanguage
  command: string
  args: string[]
  rootUri: string
}

// ─── LSP Protocol Types ─────────────────────────────────────────────────────

export interface LSPPosition {
  line: number
  character: number
}

export interface LSPRange {
  start: LSPPosition
  end: LSPPosition
}

export interface LSPLocation {
  uri: string
  range: LSPRange
}

export interface LSPCompletionItem {
  label: string
  kind?: number
  detail?: string
  documentation?: string | { kind: string; value: string }
  insertText?: string
}

export interface LSPHoverResult {
  contents:
    | string
    | { kind: string; value: string }
    | Array<string | { language: string; value: string }>
  range?: LSPRange
}

export interface LSPProtocolDiagnostic {
  range: LSPRange
  severity?: 1 | 2 | 3 | 4 // Error, Warning, Information, Hint
  code?: number | string
  source?: string
  message: string
}

export interface LSPPublishDiagnosticsParams {
  uri: string
  diagnostics: LSPProtocolDiagnostic[]
}

// ─── Document Symbols ─────────────────────────────────────────────────────

export interface LSPDocumentSymbol {
  name: string
  kind: number
  range: LSPRange
  children?: LSPDocumentSymbol[]
  detail?: string
  containerName?: string
}

// ─── Workspace Symbols ────────────────────────────────────────────────────

export interface LSPWorkspaceSymbol {
  name: string
  kind: number
  location: { uri: string; range: LSPRange }
}

// ─── Code Actions ─────────────────────────────────────────────────────────

export interface LSPTextEdit {
  range: LSPRange
  newText: string
}

export interface LSPWorkspaceEdit {
  changes: Record<string, LSPTextEdit[]>
}

export interface LSPCodeAction {
  title: string
  kind?: string
  diagnostics?: LSPProtocolDiagnostic[]
  edit?: LSPWorkspaceEdit
  command?: { title: string; command: string; arguments?: unknown[] }
}

// ─── Server Capabilities ──────────────────────────────────────────────────

export interface LSPServerCapabilities {
  completionProvider?: Record<string, unknown>
  hoverProvider?: boolean | Record<string, unknown>
  definitionProvider?: boolean | Record<string, unknown>
  referencesProvider?: boolean | Record<string, unknown>
  documentSymbolProvider?: boolean | Record<string, unknown>
  workspaceSymbolProvider?: boolean | Record<string, unknown>
  codeActionProvider?: boolean | Record<string, unknown>
  renameProvider?: boolean | Record<string, unknown>
  diagnosticProvider?: Record<string, unknown>
  textDocumentSync?: number | { openClose?: boolean; change?: number }
  [key: string]: unknown
}

export interface LSPInitializeResult {
  capabilities: LSPServerCapabilities
  serverInfo?: { name: string; version?: string }
}
