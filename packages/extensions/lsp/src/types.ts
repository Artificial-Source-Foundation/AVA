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

export interface LSPServerCapabilities {
  completionProvider?: Record<string, unknown>
  hoverProvider?: boolean | Record<string, unknown>
  definitionProvider?: boolean | Record<string, unknown>
  referencesProvider?: boolean | Record<string, unknown>
  diagnosticProvider?: Record<string, unknown>
  textDocumentSync?: number | { openClose?: boolean; change?: number }
  [key: string]: unknown
}

export interface LSPInitializeResult {
  capabilities: LSPServerCapabilities
  serverInfo?: { name: string; version?: string }
}
