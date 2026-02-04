/**
 * Tool System
 * Platform-agnostic LLM tool integration
 */

export { bashTool } from './bash.js'
// Export browser tool
export {
  type BrowserAction,
  type BrowserActionResult,
  BrowserSession,
  type BrowserSessionConfig,
  type BrowserState,
  browserTool,
  closeAllSessions,
  getActiveSessions,
} from './browser/index.js'
// Export completion tool
export {
  completionTool,
  formatCompletionResult,
  getCompletionDetails,
  isCompletionCall,
  resetCompletionState,
  wasCompletionAttempted,
} from './completion.js'
export { createTool } from './create.js'
// Export defineTool pattern (OpenCode-inspired)
export {
  type DefinedTool,
  defineTool,
  getToolLocations,
  getToolPermissions,
  isDefinedTool,
  type ToolConfig,
  type ToolExample,
  type ToolPermission,
} from './define.js'
export { deleteTool } from './delete.js'
export { editTool, replace as editReplace } from './edit.js'
// Export edit replacers for customization
export {
  DEFAULT_REPLACERS,
  levenshtein,
  normalizeLineEndings,
  type Replacer,
  similarity,
} from './edit-replacers.js'
// Export errors
export { ToolError, ToolErrorType } from './errors.js'
// Export individual tools
export { globTool } from './glob.js'
export { grepTool } from './grep.js'
// Export file locks
export {
  clearAllLocks,
  getActiveLocks,
  getLockInfo,
  isFileLocked,
  type LockInfo,
  tryFileLock,
  withFileLock,
} from './locks.js'
export { lsTool } from './ls.js'
export { questionTool } from './question.js'
export { readTool } from './read.js'
export {
  executeTool,
  getToolDefinitions,
  registerTool,
  resetToolCallCount,
} from './registry.js'
// Export registry
// Export sanitization
export {
  detectSanitizeModelFamily,
  ensureTrailingNewline,
  extractFenceLanguage,
  hasMarkdownFences,
  normalizeLineEndings as normalizeLineEndingsSanitize,
  type SanitizeModelFamily,
  type SanitizeOptions,
  sanitizeContent,
  stripMarkdownFences,
  trimTrailingWhitespace,
} from './sanitize.js'
export { taskTool } from './task.js'
export { clearTodos, getTodos, setTodos, todoReadTool, todoWriteTool } from './todo.js'
// Export truncation module
export {
  cleanupOutputFiles,
  TRUNCATION_LIMITS,
  type TruncationOptions,
  type TruncationResult as EnhancedTruncationResult,
  truncateForMetadata,
  truncateLine,
  truncateOutput as truncateOutputEnhanced,
} from './truncation.js'
// Export types (ToolDefinition is exported from types/llm.js via main index)
export type {
  MetadataCallback,
  MetadataUpdate,
  Tool,
  ToolContext,
  ToolLocation,
  ToolResult,
} from './types.js'
// Export utilities
export {
  type BinaryCheckResult,
  checkBinaryFile,
  type FileSuggestion,
  findSimilarFiles,
  formatLineNumber,
  formatSuggestions,
  isBinaryExtension,
  isBinaryFile,
  isBinaryOutput,
  LIMITS,
  matchesGlob,
  resolvePath,
  shouldSkipDirectory,
  type TruncationResult,
  truncate,
  truncateOutput,
} from './utils.js'
// Export validation (Zod helpers)
export { commonSchemas, formatZodError, isZodSchema } from './validation.js'
export { webfetchTool } from './webfetch.js'
export { websearchTool } from './websearch.js'
export { writeTool } from './write.js'

// ============================================================================
// Auto-register tools on import
// ============================================================================

import { planEnterTool, planExitTool } from '../agent/modes/index.js'
import { bashTool } from './bash.js'
import { browserTool } from './browser/index.js'
import { completionTool } from './completion.js'
import { createTool } from './create.js'
import { deleteTool } from './delete.js'
import { editTool } from './edit.js'
import { globTool } from './glob.js'
import { grepTool } from './grep.js'
import { lsTool } from './ls.js'
import { questionTool } from './question.js'
import { readTool } from './read.js'
import { registerTool } from './registry.js'
import { taskTool } from './task.js'
import { todoReadTool, todoWriteTool } from './todo.js'
import { webfetchTool } from './webfetch.js'
import { websearchTool } from './websearch.js'
import { writeTool } from './write.js'

// Register built-in tools
registerTool(globTool)
registerTool(readTool)
registerTool(grepTool)
registerTool(createTool)
registerTool(writeTool)
registerTool(deleteTool)
registerTool(bashTool)
registerTool(editTool)
registerTool(lsTool)
registerTool(questionTool)
registerTool(todoReadTool)
registerTool(todoWriteTool)
registerTool(taskTool)
registerTool(webfetchTool)
registerTool(websearchTool)
registerTool(completionTool)
registerTool(browserTool)
registerTool(planEnterTool)
registerTool(planExitTool)
