/**
 * Tree-sitter Integration
 * Code analysis using tree-sitter parsers
 *
 * Currently supports:
 * - Bash command analysis
 *
 * Future:
 * - TypeScript/JavaScript parsing
 * - Python parsing
 * - Go parsing
 */

// Bash analysis
export {
  analyzeBash,
  getAffectedPaths,
  getCommandRiskSummary,
  isSafeCommand,
} from './bash.js'

// Types
export {
  type BashAnalysis,
  type BashCommand,
  type BashPath,
  CONDITIONALLY_DESTRUCTIVE,
  DESTRUCTIVE_COMMANDS,
  ELEVATION_COMMANDS,
  SAFE_COMMANDS,
  SYSTEM_COMMANDS,
} from './types.js'
