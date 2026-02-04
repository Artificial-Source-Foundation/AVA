/**
 * LSP Integration
 * Language Server Protocol support for enhanced code intelligence
 *
 * Currently provides:
 * - Diagnostic extraction from common tools (tsc, eslint, pyright, go)
 * - Diagnostic formatting and summarization
 * - Call hierarchy (incoming/outgoing calls)
 *
 * Supported languages:
 * - TypeScript/JavaScript (tsc, eslint)
 * - Python (pyright)
 * - Go (go vet)
 * - Rust (rust-analyzer)
 * - Java (jdtls)
 */

// Call Hierarchy
export {
  type CallHierarchyCallsResult,
  type CallHierarchyIncomingCall,
  type CallHierarchyItem,
  type CallHierarchyOutgoingCall,
  getCallHierarchyExtensions,
  getIncomingCalls,
  getOutgoingCalls,
  getTypeScriptCallHierarchy,
  type PrepareCallHierarchyResult,
  SymbolKind,
} from './call-hierarchy.js'
// Diagnostics
export {
  formatDiagnostics,
  getDiagnostics,
  getEslintDiagnostics,
  getGoDiagnostics,
  getPythonDiagnostics,
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
