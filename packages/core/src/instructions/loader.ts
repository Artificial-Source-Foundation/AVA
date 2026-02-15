/**
 * Instructions Loader
 * Load instruction files from project, directory, and global locations
 */

import { getPlatform } from '../platform.js'
import type {
  InstructionConfig,
  InstructionFile,
  InstructionResult,
  InstructionSource,
} from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Default instruction file names (in priority order) */
const DEFAULT_FILENAMES = ['AGENTS.md', 'CLAUDE.md', 'instructions.md']

/** Default global instructions directory */
const DEFAULT_GLOBAL_DIR = '~/.ava'

/** Maximum files to load */
const DEFAULT_MAX_FILES = 10

/** Maximum content length per file (100KB) */
const DEFAULT_MAX_CONTENT_LENGTH = 100 * 1024

// ============================================================================
// Instruction Loader
// ============================================================================

/**
 * Loads instruction files from the filesystem
 */
export class InstructionLoader {
  private readonly config: Required<InstructionConfig>
  private readonly loadedPaths = new Set<string>()

  constructor(config: InstructionConfig = {}) {
    this.config = {
      filenames: config.filenames ?? DEFAULT_FILENAMES,
      globalDir: config.globalDir ?? DEFAULT_GLOBAL_DIR,
      maxFiles: config.maxFiles ?? DEFAULT_MAX_FILES,
      maxContentLength: config.maxContentLength ?? DEFAULT_MAX_CONTENT_LENGTH,
    }
  }

  /**
   * Get instructions for a specific file path
   * Walks up from the file's directory to the project root
   */
  async getInstructionsForPath(
    filepath: string,
    projectRoot: string,
    alreadyLoaded: Set<string> = new Set()
  ): Promise<InstructionResult> {
    const files: InstructionFile[] = []
    const notFound: string[] = []

    // Start from file's directory
    const fs = getPlatform().fs
    let currentDir: string

    try {
      const stat = await fs.stat(filepath)
      currentDir = stat.isDirectory ? filepath : filepath.substring(0, filepath.lastIndexOf('/'))
    } catch {
      // If file doesn't exist, start from project root
      currentDir = projectRoot
    }

    // Normalize project root
    projectRoot = projectRoot.replace(/\/$/, '')

    // Walk up to project root
    while (currentDir.startsWith(projectRoot) || currentDir === projectRoot) {
      if (files.length >= this.config.maxFiles) {
        break
      }

      const found = await this.loadInstructionsFromDir(
        currentDir,
        currentDir === projectRoot ? 'project' : 'directory',
        alreadyLoaded
      )

      if (found) {
        files.push(found)
        alreadyLoaded.add(found.filepath)
      } else {
        notFound.push(currentDir)
      }

      // Move up
      if (currentDir === projectRoot) {
        break
      }
      const parentDir = currentDir.substring(0, currentDir.lastIndexOf('/'))
      if (parentDir === currentDir || parentDir.length < projectRoot.length) {
        break
      }
      currentDir = parentDir
    }

    // Load global instructions
    if (files.length < this.config.maxFiles) {
      const globalPath = this.resolveGlobalPath()
      if (!alreadyLoaded.has(globalPath)) {
        const globalFound = await this.loadInstructionsFromDir(globalPath, 'global', alreadyLoaded)
        if (globalFound) {
          files.push(globalFound)
          alreadyLoaded.add(globalFound.filepath)
        }
      }
    }

    // Combine content
    const combinedContent = this.combineInstructions(files)

    return { files, combinedContent, notFound }
  }

  /**
   * Load instructions from a single directory
   */
  private async loadInstructionsFromDir(
    dirPath: string,
    source: InstructionSource,
    alreadyLoaded: Set<string>
  ): Promise<InstructionFile | null> {
    const fs = getPlatform().fs

    for (const filename of this.config.filenames) {
      const filepath = `${dirPath}/${filename}`

      if (alreadyLoaded.has(filepath)) {
        continue
      }

      try {
        const stat = await fs.stat(filepath)
        if (!stat.isDirectory) {
          let content = await fs.readFile(filepath)

          // Truncate if too long
          if (content.length > this.config.maxContentLength) {
            content = `${content.slice(0, this.config.maxContentLength)}\n\n[... truncated ...]`
          }

          return {
            filepath,
            content,
            source,
            appliesTo: dirPath,
          }
        }
      } catch {}
    }

    return null
  }

  /**
   * Resolve the global instructions directory
   */
  private resolveGlobalPath(): string {
    const globalDir = this.config.globalDir

    // Expand ~ to home directory
    if (globalDir.startsWith('~')) {
      const home = process.env.HOME ?? process.env.USERPROFILE ?? ''
      return globalDir.replace('~', home)
    }

    return globalDir
  }

  /**
   * Combine instruction files into a single string
   */
  private combineInstructions(files: InstructionFile[]): string {
    if (files.length === 0) {
      return ''
    }

    const sections: string[] = []

    for (const file of files) {
      const header = `<!-- Instructions from ${file.source}: ${file.filepath} -->`
      sections.push(`${header}\n\n${file.content}`)
    }

    return sections.join('\n\n---\n\n')
  }

  /**
   * Format instructions for injection into tool output
   */
  formatForInjection(result: InstructionResult): string {
    if (result.files.length === 0) {
      return ''
    }

    return `<system-reminder>\n${result.combinedContent}\n</system-reminder>`
  }

  /**
   * Clear loaded paths cache (for testing)
   */
  clearCache(): void {
    this.loadedPaths.clear()
  }
}

// ============================================================================
// Factory Function
// ============================================================================

/**
 * Create an instruction loader with default configuration
 */
export function createInstructionLoader(config?: InstructionConfig): InstructionLoader {
  return new InstructionLoader(config)
}

// ============================================================================
// Singleton
// ============================================================================

let globalLoader: InstructionLoader | undefined

/**
 * Get the global instruction loader
 */
export function getInstructionLoader(): InstructionLoader {
  if (!globalLoader) {
    globalLoader = new InstructionLoader()
  }
  return globalLoader
}

/**
 * Set the global instruction loader
 */
export function setInstructionLoader(loader: InstructionLoader): void {
  globalLoader = loader
}
