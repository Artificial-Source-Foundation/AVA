/**
 * Tool System
 * LLM tool integration for file operations
 */

// Export errors

// Export tools

// Export registry
export {
  executeTool,
  getToolDefinitions,
  resetToolCallCount,
} from './registry'
// Export types
export type { ToolContext } from './types'

// Export utilities

// ============================================================================
// Auto-register tools on import
// ============================================================================

import { bashTool } from './bash'
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
registerTool(bashTool)
