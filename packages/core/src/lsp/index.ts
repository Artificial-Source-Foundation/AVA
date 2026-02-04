/**
 * LSP Integration
 * Language Server Protocol support for enhanced code intelligence
 *
 * Currently provides:
 * - Diagnostic extraction from common tools (tsc, eslint)
 * - Diagnostic formatting and summarization
 *
 * Future enhancements:
 * - Full LSP client connection
 * - Hover information
 * - Go to definition
 * - Code actions
 */

// Diagnostics
export {
  formatDiagnostics,
  getDiagnostics,
  getEslintDiagnostics,
  getTypeScriptDiagnostics,
  hasErrors,
  summarizeDiagnostics,
} from './diagnostics.js'

// Types
export {
  DEFAULT_SERVER_CONFIGS,
  type Diagnostic,
  type DiagnosticRelatedInformation,
  type DiagnosticResult,
  DiagnosticSeverity,
  type DiagnosticSummary,
  type LanguageServerConfig,
  type LanguageServerId,
  type Position,
  type Range,
} from './types.js'
