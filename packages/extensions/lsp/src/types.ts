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
