/**
 * Custom Command Discovery
 * Scans directories for TOML command files and resolves command names.
 *
 * Discovery order (higher priority wins):
 * 1. Project-level: <project>/.estela/commands/
 * 2. User-level: ~/.estela/commands/
 *
 * Naming convention:
 * - commands/test.toml → "test"
 * - commands/git/commit.toml → "git:commit"
 * - Subdirectories create namespaced commands with ":" separator
 */

import { readdir, stat } from 'node:fs/promises'
import { extname, join, relative } from 'node:path'
import type { CommandDiscoveryConfig, CommandFileInfo } from './types.js'

// ============================================================================
// Constants
// ============================================================================

const TOML_EXT = '.toml'
const NAMESPACE_SEPARATOR = ':'
const COMMANDS_DIR_NAME = 'commands'
const CONFIG_DIR_NAME = '.estela'

// ============================================================================
// Discovery
// ============================================================================

/**
 * Discover all custom command files across configured directories.
 * Project-level commands take priority over user-level commands.
 *
 * @param config - Discovery configuration
 * @returns Array of discovered command files, deduplicated by name (project wins)
 */
export async function discoverCommands(config: CommandDiscoveryConfig): Promise<CommandFileInfo[]> {
  const commandMap = new Map<string, CommandFileInfo>()

  // Load user-level commands first (lower priority)
  if (config.userDir) {
    const userCommands = await scanDirectory(config.userDir, false)
    for (const cmd of userCommands) {
      commandMap.set(cmd.name, cmd)
    }
  }

  // Load extra directories (medium priority)
  if (config.extraDirs) {
    for (const dir of config.extraDirs) {
      const commands = await scanDirectory(dir, false)
      for (const cmd of commands) {
        commandMap.set(cmd.name, cmd)
      }
    }
  }

  // Load project-level commands last (highest priority, overwrites)
  if (config.projectDir) {
    const projectCommands = await scanDirectory(config.projectDir, true)
    for (const cmd of projectCommands) {
      commandMap.set(cmd.name, cmd)
    }
  }

  // Return sorted by name for deterministic ordering
  return Array.from(commandMap.values()).sort((a, b) => a.name.localeCompare(b.name))
}

/**
 * Scan a directory recursively for TOML command files.
 *
 * @param dir - Directory to scan
 * @param isProjectLevel - Whether these are project-level commands
 * @returns Array of command file info
 */
async function scanDirectory(dir: string, isProjectLevel: boolean): Promise<CommandFileInfo[]> {
  const commands: CommandFileInfo[] = []

  try {
    await scanDirectoryRecursive(dir, dir, isProjectLevel, commands)
  } catch (err) {
    // Directory doesn't exist or isn't readable - not an error
    if (!isNotFoundError(err)) {
      console.warn(`Failed to scan command directory ${dir}:`, err)
    }
  }

  return commands
}

/**
 * Recursive directory scanner
 */
async function scanDirectoryRecursive(
  baseDir: string,
  currentDir: string,
  isProjectLevel: boolean,
  results: CommandFileInfo[]
): Promise<void> {
  let entries: string[]
  try {
    entries = await readdir(currentDir)
  } catch {
    return
  }

  for (const entry of entries) {
    // Skip hidden files
    if (entry.startsWith('.')) continue

    const fullPath = join(currentDir, entry)
    let stats: Awaited<ReturnType<typeof stat>> | undefined

    try {
      stats = await stat(fullPath)
    } catch {
      continue
    }

    if (stats.isDirectory()) {
      // Recurse into subdirectory
      await scanDirectoryRecursive(baseDir, fullPath, isProjectLevel, results)
    } else if (stats.isFile() && entry.endsWith(TOML_EXT)) {
      // Convert file path to command name
      const name = filePathToCommandName(baseDir, fullPath)
      results.push({
        filePath: fullPath,
        name,
        isProjectLevel,
      })
    }
  }
}

// ============================================================================
// Naming
// ============================================================================

/**
 * Convert a file path to a command name.
 *
 * Examples:
 *   commands/test.toml → "test"
 *   commands/git/commit.toml → "git:commit"
 *   commands/code/review/frontend.toml → "code:review:frontend"
 */
function filePathToCommandName(baseDir: string, filePath: string): string {
  const relativePath = relative(baseDir, filePath)

  // Remove extension, then replace directory separators with namespace separator
  const withoutExt = relativePath.slice(0, -extname(relativePath).length)
  return withoutExt.replace(/[/\\]/g, NAMESPACE_SEPARATOR).toLowerCase()
}

// ============================================================================
// Factory Helpers
// ============================================================================

/**
 * Create a standard discovery config for a project.
 *
 * @param workspaceRoot - Project root directory
 * @returns Discovery config with project and user directories
 */
export function createDiscoveryConfig(workspaceRoot: string): CommandDiscoveryConfig {
  const home = process.env.HOME ?? process.env.USERPROFILE ?? '.'

  return {
    projectDir: join(workspaceRoot, CONFIG_DIR_NAME, COMMANDS_DIR_NAME),
    userDir: join(home, CONFIG_DIR_NAME, COMMANDS_DIR_NAME),
  }
}

/**
 * Get the project commands directory for a workspace
 */
export function getProjectCommandsDir(workspaceRoot: string): string {
  return join(workspaceRoot, CONFIG_DIR_NAME, COMMANDS_DIR_NAME)
}

/**
 * Get the user commands directory
 */
export function getUserCommandsDir(): string {
  const home = process.env.HOME ?? process.env.USERPROFILE ?? '.'
  return join(home, CONFIG_DIR_NAME, COMMANDS_DIR_NAME)
}

// ============================================================================
// Helpers
// ============================================================================

function isNotFoundError(err: unknown): boolean {
  return err instanceof Error && 'code' in err && (err as NodeJS.ErrnoException).code === 'ENOENT'
}
