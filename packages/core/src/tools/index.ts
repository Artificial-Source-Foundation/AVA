/**
 * Tool System
 * Platform-agnostic LLM tool integration
 */

export { bashTool } from './bash.js'
export { createTool } from './create.js'
export { deleteTool } from './delete.js'
// Export errors
export { ToolError, ToolErrorType } from './errors.js'

// Export individual tools
export { globTool } from './glob.js'
export { grepTool } from './grep.js'
export { readTool } from './read.js'
// Export registry
export {
  executeTool,
  getToolDefinitions,
  registerTool,
  resetToolCallCount,
} from './registry.js'
// Export types (ToolDefinition is exported from types/llm.js via main index)
export type {
  Tool,
  ToolContext,
  ToolLocation,
  ToolResult,
} from './types.js'
// Export utilities
export {
  formatLineNumber,
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
export { writeTool } from './write.js'

// ============================================================================
// Auto-register tools on import
// ============================================================================

import { bashTool } from './bash.js'
import { createTool } from './create.js'
import { deleteTool } from './delete.js'
import { globTool } from './glob.js'
import { grepTool } from './grep.js'
import { readTool } from './read.js'
import { registerTool } from './registry.js'
import { writeTool } from './write.js'

// Register built-in tools
registerTool(globTool)
registerTool(readTool)
registerTool(grepTool)
registerTool(createTool)
registerTool(writeTool)
registerTool(deleteTool)
registerTool(bashTool)
