/**
 * Tool System
 * LLM tool integration for file operations
 */

export { createTool } from './create'
export { deleteTool } from './delete'
// Export errors
export { getToolErrorMessage, ToolError, ToolErrorType } from './errors'
// Export tools
export { globTool } from './glob'
export { grepTool } from './grep'
export { readTool } from './read'
// Export registry
export {
  executeTool,
  executeTools,
  getAllTools,
  getTool,
  getToolCallCount,
  getToolDefinitions,
  hasTool,
  registerTool,
  resetToolCallCount,
} from './registry'
// Export types
export type {
  ContentBlock,
  MessageWithBlocks,
  TextBlock,
  Tool,
  ToolContext,
  ToolDefinition,
  ToolParameterProperty,
  ToolParameterSchema,
  ToolResult,
  ToolResultBlock,
  ToolUseBlock,
} from './types'
// Export utilities
export {
  formatLineNumber,
  getRelativePath,
  isBinaryExtension,
  isBinaryFile,
  isPathInside,
  LIMITS,
  matchesGlob,
  resolvePath,
  shouldSkipDirectory,
  truncate,
} from './utils'
export { writeTool } from './write'

// ============================================================================
// Auto-register tools on import
// ============================================================================

import { createTool } from './create'
import { deleteTool } from './delete'
import { globTool } from './glob'
import { grepTool } from './grep'
import { readTool } from './read'
import { registerTool } from './registry'
import { writeTool } from './write'

// Register built-in tools
registerTool(globTool)
registerTool(readTool)
registerTool(grepTool)
registerTool(createTool)
registerTool(writeTool)
registerTool(deleteTool)
