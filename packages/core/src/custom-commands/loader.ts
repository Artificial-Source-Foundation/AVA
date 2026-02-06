/**
 * Custom Command Loader
 * Discovers, parses, and registers TOML commands into the slash command registry.
 *
 * Usage:
 * ```ts
 * const loader = new CustomCommandLoader(registry)
 * await loader.loadAll('/path/to/project')
 * ```
 */

import { readFile } from 'node:fs/promises'
import type { CommandRegistry } from '../slash-commands/registry.js'
import type { CommandContext, CommandResult, SlashCommand } from '../slash-commands/types.js'
import { createDiscoveryConfig, discoverCommands } from './discovery.js'
import { parseCommandToml } from './parser.js'
import { resolveTemplate } from './template.js'
import type { CustomCommandDef, ShellExecution } from './types.js'

// ============================================================================
// Loader
// ============================================================================

/**
 * Loads and registers custom TOML commands into a CommandRegistry.
 */
export class CustomCommandLoader {
  private registry: CommandRegistry
  private loadedCommands: Map<string, CustomCommandDef> = new Map()

  constructor(registry: CommandRegistry) {
    this.registry = registry
  }

  // ==========================================================================
  // Loading
  // ==========================================================================

  /**
   * Discover and load all custom commands for a workspace.
   *
   * @param workspaceRoot - Project root directory
   * @returns Number of commands loaded
   */
  async loadAll(workspaceRoot: string): Promise<number> {
    const config = createDiscoveryConfig(workspaceRoot)
    const files = await discoverCommands(config)

    let loadedCount = 0
    for (const file of files) {
      try {
        const content = await readFile(file.filePath, 'utf-8')
        const def = parseCommandToml(content, file.name, file.filePath, file.isProjectLevel)

        this.loadedCommands.set(def.name, def)
        this.registerCommand(def, workspaceRoot)
        loadedCount++
      } catch (err) {
        console.warn(
          `Failed to load custom command "${file.name}" from ${file.filePath}:`,
          err instanceof Error ? err.message : err
        )
      }
    }

    return loadedCount
  }

  /**
   * Load a single command from a TOML file.
   *
   * @param filePath - Path to TOML file
   * @param name - Command name
   * @param workspaceRoot - Project root
   * @param isProjectLevel - Whether project-level
   * @returns Loaded command definition
   */
  async loadSingle(
    filePath: string,
    name: string,
    workspaceRoot: string,
    isProjectLevel = false
  ): Promise<CustomCommandDef> {
    const content = await readFile(filePath, 'utf-8')
    const def = parseCommandToml(content, name, filePath, isProjectLevel)

    this.loadedCommands.set(def.name, def)
    this.registerCommand(def, workspaceRoot)

    return def
  }

  // ==========================================================================
  // Registration
  // ==========================================================================

  /**
   * Convert a CustomCommandDef into a SlashCommand and register it.
   */
  private registerCommand(def: CustomCommandDef, workspaceRoot: string): void {
    const command: SlashCommand = {
      name: def.name,
      description: def.description ?? `Custom command from ${def.sourcePath}`,
      usage: `/${def.name} [args]`,
      execute: (ctx) => this.executeCustomCommand(def, ctx, workspaceRoot),
    }

    this.registry.register(command, 'custom')
  }

  /**
   * Execute a custom command by resolving its template.
   */
  private async executeCustomCommand(
    def: CustomCommandDef,
    ctx: CommandContext,
    workspaceRoot: string
  ): Promise<CommandResult> {
    const result = await resolveTemplate(def.prompt, ctx.rawArgs, {
      workingDirectory: workspaceRoot,
      readFile: async (path) => {
        const { resolve } = await import('node:path')
        const fullPath = resolve(workspaceRoot, path)
        return readFile(fullPath, 'utf-8')
      },
      executeShell: async (command): Promise<ShellExecution> => {
        try {
          const { exec } = await import('node:child_process')
          const { promisify } = await import('node:util')
          const execAsync = promisify(exec)

          const { stdout, stderr } = await execAsync(command, {
            cwd: workspaceRoot,
            timeout: 30000,
            maxBuffer: 1024 * 1024, // 1MB
          })

          return {
            command,
            output: stdout,
            stderr,
            exitCode: 0,
            success: true,
          }
        } catch (err) {
          const execError = err as { stdout?: string; stderr?: string; code?: number }
          return {
            command,
            output: execError.stdout ?? '',
            stderr: execError.stderr ?? (err instanceof Error ? err.message : 'Unknown error'),
            exitCode: execError.code ?? 1,
            success: false,
          }
        }
      },
    })

    // Custom commands inject the resolved prompt as context data
    // The agent loop will send this as a user message to the LLM
    return {
      success: !result.hasErrors,
      message: result.hasErrors
        ? 'Command executed with some errors (see prompt for details)'
        : undefined,
      contextData: result.prompt,
      stopProcessing: false,
    }
  }

  // ==========================================================================
  // Query
  // ==========================================================================

  /**
   * Get all loaded custom command definitions
   */
  getLoadedCommands(): CustomCommandDef[] {
    return Array.from(this.loadedCommands.values())
  }

  /**
   * Get a specific loaded command
   */
  getCommand(name: string): CustomCommandDef | undefined {
    return this.loadedCommands.get(name)
  }

  /**
   * Check if a command name is a custom command
   */
  isCustomCommand(name: string): boolean {
    return this.loadedCommands.has(name)
  }

  /**
   * Unload all custom commands
   */
  clear(): void {
    for (const name of this.loadedCommands.keys()) {
      this.registry.unregister(name)
    }
    this.loadedCommands.clear()
  }

  /**
   * Reload all commands (clear and re-discover)
   */
  async reload(workspaceRoot: string): Promise<number> {
    this.clear()
    return this.loadAll(workspaceRoot)
  }
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create a custom command loader with a registry.
 *
 * @param registry - Command registry to register commands into
 * @returns CustomCommandLoader instance
 */
export function createCustomCommandLoader(registry: CommandRegistry): CustomCommandLoader {
  return new CustomCommandLoader(registry)
}
