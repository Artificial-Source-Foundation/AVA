/**
 * LSP Types
 * Type definitions for Language Server Protocol integration
 */

// ============================================================================
// Diagnostic Types
// ============================================================================

/**
 * Diagnostic severity levels (matching LSP spec)
 */
export enum DiagnosticSeverity {
  Error = 1,
  Warning = 2,
  Information = 3,
  Hint = 4,
}

/**
 * A range in a text document
 */
export interface Range {
  start: Position
  end: Position
}

/**
 * A position in a text document
 */
export interface Position {
  /** Line number (0-based) */
  line: number
  /** Character offset (0-based) */
  character: number
}

/**
 * A diagnostic message from an LSP server
 */
export interface Diagnostic {
  /** The range this diagnostic applies to */
  range: Range
  /** Severity level */
  severity: DiagnosticSeverity
  /** The diagnostic's message */
  message: string
  /** Error/warning code */
  code?: string | number
  /** Source of the diagnostic (e.g., "typescript", "eslint") */
  source?: string
  /** Related information */
  relatedInformation?: DiagnosticRelatedInformation[]
}

/**
 * Related information for a diagnostic
 */
export interface DiagnosticRelatedInformation {
  /** Location of the related information */
  location: {
    uri: string
    range: Range
  }
  /** Message */
  message: string
}

// ============================================================================
// LSP Server Configuration
// ============================================================================

/**
 * Supported language servers
 */
export type LanguageServerId = 'typescript' | 'python' | 'go' | 'rust' | 'eslint'

/**
 * Configuration for a language server
 */
export interface LanguageServerConfig {
  /** Server identifier */
  id: LanguageServerId
  /** Command to start the server */
  command: string
  /** Arguments for the command */
  args: string[]
  /** File extensions this server handles */
  extensions: string[]
  /** Root markers (files that indicate project root) */
  rootMarkers: string[]
  /** Whether server supports diagnostics */
  supportsDiagnostics: boolean
}

/**
 * Default language server configurations
 */
export const DEFAULT_SERVER_CONFIGS: Record<LanguageServerId, LanguageServerConfig> = {
  typescript: {
    id: 'typescript',
    command: 'npx',
    args: ['typescript-language-server', '--stdio'],
    extensions: ['.ts', '.tsx', '.js', '.jsx'],
    rootMarkers: ['tsconfig.json', 'jsconfig.json', 'package.json'],
    supportsDiagnostics: true,
  },
  python: {
    id: 'python',
    command: 'pyright-langserver',
    args: ['--stdio'],
    extensions: ['.py', '.pyi'],
    rootMarkers: ['pyrightconfig.json', 'pyproject.toml', 'setup.py'],
    supportsDiagnostics: true,
  },
  go: {
    id: 'go',
    command: 'gopls',
    args: ['serve'],
    extensions: ['.go'],
    rootMarkers: ['go.mod', 'go.sum'],
    supportsDiagnostics: true,
  },
  rust: {
    id: 'rust',
    command: 'rust-analyzer',
    args: [],
    extensions: ['.rs'],
    rootMarkers: ['Cargo.toml'],
    supportsDiagnostics: true,
  },
  eslint: {
    id: 'eslint',
    command: 'npx',
    args: ['eslint_d', '--stdin', '--stdin-filename'],
    extensions: ['.js', '.jsx', '.ts', '.tsx'],
    rootMarkers: ['.eslintrc', '.eslintrc.js', '.eslintrc.json', 'eslint.config.js'],
    supportsDiagnostics: true,
  },
}

// ============================================================================
// Diagnostic Results
// ============================================================================

/**
 * Result of getting diagnostics for a file
 */
export interface DiagnosticResult {
  /** File path */
  file: string
  /** List of diagnostics */
  diagnostics: Diagnostic[]
  /** Source that provided the diagnostics */
  source: string
  /** Time taken to get diagnostics (ms) */
  durationMs?: number
}

/**
 * Summary of diagnostics
 */
export interface DiagnosticSummary {
  /** Total number of errors */
  errors: number
  /** Total number of warnings */
  warnings: number
  /** Total number of hints/info */
  hints: number
  /** Files with errors */
  filesWithErrors: string[]
}
