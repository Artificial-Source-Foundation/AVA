/**
 * @ava/core Instructions Module
 * Load project and directory instructions for context injection
 */

// Loader
export {
  createInstructionLoader,
  getInstructionLoader,
  InstructionLoader,
  setInstructionLoader,
} from './loader.js'
// Types
export type {
  InstructionConfig,
  InstructionFile,
  InstructionResult,
  InstructionSource,
} from './types.js'
