/**
 * Instructions Types
 * Types for project and directory instruction loading
 */

// ============================================================================
// Types
// ============================================================================

/**
 * Source of an instruction file
 */
export type InstructionSource = 'project' | 'directory' | 'global'

/**
 * An instruction file loaded from the filesystem
 */
export interface InstructionFile {
  /** Absolute path to the instruction file */
  filepath: string
  /** Content of the instruction file */
  content: string
  /** Source type */
  source: InstructionSource
  /** Directory the instruction applies to */
  appliesTo: string
}

/**
 * Configuration for instruction loading
 */
export interface InstructionConfig {
  /** File names to look for (in priority order) */
  filenames?: string[]
  /** Global instructions directory */
  globalDir?: string
  /** Maximum number of instruction files to load */
  maxFiles?: number
  /** Maximum content length per file */
  maxContentLength?: number
}

/**
 * Result of loading instructions for a path
 */
export interface InstructionResult {
  /** All loaded instruction files */
  files: InstructionFile[]
  /** Combined content for injection */
  combinedContent: string
  /** Paths that were checked but not found */
  notFound: string[]
}
