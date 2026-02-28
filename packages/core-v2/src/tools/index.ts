// Tool system

export { bashTool } from './bash.js'
export type { DefinedTool, ToolConfig, ToolExample, ToolPermission } from './define.js'
export { defineTool, getToolLocations, getToolPermissions, isDefinedTool } from './define.js'
export { editTool } from './edit.js'
export type { Replacer } from './edit-replacers.js'
export { DEFAULT_REPLACERS, replace, similarity } from './edit-replacers.js'
export { ToolError, ToolErrorType } from './errors.js'
export { globTool } from './glob.js'
export { grepTool } from './grep.js'
// Core tools
export { readFileTool } from './read.js'
export {
  executeTool,
  getAllTools,
  getTool,
  getToolDefinitions,
  registerTool,
  resetTools,
  unregisterTool,
} from './registry.js'
export {
  hasMarkdownFences,
  normalizeLineEndings,
  sanitizeContent,
  stripMarkdownFences,
} from './sanitize.js'
export type {
  AnyTool,
  MetadataCallback,
  MetadataUpdate,
  Tool,
  ToolContext,
  ToolLocation,
  ToolResult,
} from './types.js'
export {
  formatLineNumber,
  isBinaryExtension,
  isBinaryFile,
  isBinaryOutput,
  LIMITS,
  matchesGlob,
  resolvePath,
  resolvePathSafe,
  shouldSkipDirectory,
  truncate,
  truncateOutput,
} from './utils.js'
export { formatZodError, isZodSchema } from './validation.js'
export { writeFileTool } from './write.js'

import { bashTool } from './bash.js'
import { editTool } from './edit.js'
import { globTool } from './glob.js'
import { grepTool } from './grep.js'
import { readFileTool } from './read.js'
// Auto-register core tools
import { registerTool } from './registry.js'
import { writeFileTool } from './write.js'

export function registerCoreTools(): void {
  registerTool(readFileTool)
  registerTool(writeFileTool)
  registerTool(editTool)
  registerTool(bashTool)
  registerTool(globTool)
  registerTool(grepTool)
}
