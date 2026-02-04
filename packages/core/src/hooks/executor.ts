/**
 * Hook Executor
 * Discovers and executes hook scripts
 *
 * Hook discovery order:
 * 1. Global: ~/.estela/hooks/{HookType}
 * 2. Project: .estela/hooks/{HookType}
 *
 * Both run if present (global first, then project)
 *
 * Hook protocol:
 * - Input: JSON via stdin (context specific to hook type)
 * - Output: JSON via stdout (HookResult)
 * - Timeout: 30 seconds default
 * - Non-zero exit = warning (doesn't block unless cancel: true)
 */

import { spawn } from 'node:child_process'
import * as fs from 'node:fs'
import { homedir } from 'node:os'
import * as path from 'node:path'
import {
  mergeHookResults,
  parseHookOutput,
  serializeContext,
  validateHookResult,
} from './factory.js'
import type {
  HookConfig,
  HookContext,
  HookEvent,
  HookEventListener,
  HookLocation,
  HookResult,
  HookType,
} from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Global hooks directory */
const GLOBAL_HOOKS_DIR = path.join(homedir(), '.estela', 'hooks')

/** Project hooks directory (relative to working directory) */
const PROJECT_HOOKS_DIR = '.estela/hooks'

/** All hook types in execution order */
const ALL_HOOK_TYPES: HookType[] = [
  'PreToolUse',
  'PostToolUse',
  'TaskStart',
  'TaskComplete',
  'TaskCancel',
]

// ============================================================================
// Hook Discovery
// ============================================================================

/**
 * Check if a file is executable
 */
async function isExecutable(filepath: string): Promise<boolean> {
  try {
    const stats = await fs.promises.stat(filepath)
    if (!stats.isFile()) return false

    // On Windows, check for common extensions
    if (process.platform === 'win32') {
      const ext = path.extname(filepath).toLowerCase()
      return ['.exe', '.cmd', '.bat', '.ps1', '.js', '.sh'].includes(ext)
    }

    // On Unix, check execute permission
    const mode = stats.mode
    const isOwnerExec = (mode & 0o100) !== 0
    const isGroupExec = (mode & 0o010) !== 0
    const isOtherExec = (mode & 0o001) !== 0

    return isOwnerExec || isGroupExec || isOtherExec
  } catch {
    return false
  }
}

/**
 * Find hook script for a given type in a directory
 */
async function findHookScript(
  dir: string,
  hookType: HookType,
  source: 'global' | 'project'
): Promise<HookLocation | null> {
  // Try exact match first
  const exactPath = path.join(dir, hookType)
  if (await isExecutable(exactPath)) {
    return { path: exactPath, source, type: hookType }
  }

  // Try with common extensions
  const extensions = ['.sh', '.js', '.ts', '.py', '.rb', '.ps1', '.cmd', '.bat']
  for (const ext of extensions) {
    const extPath = path.join(dir, `${hookType}${ext}`)
    if (await isExecutable(extPath)) {
      return { path: extPath, source, type: hookType }
    }
  }

  return null
}

/**
 * Discover all hooks for a given type
 *
 * @param hookType - The type of hook to find
 * @param workingDirectory - Project working directory
 * @returns Array of discovered hook locations (global first, then project)
 */
export async function discoverHooks(
  hookType: HookType,
  workingDirectory: string
): Promise<HookLocation[]> {
  const locations: HookLocation[] = []

  // Check global hooks
  const globalHook = await findHookScript(GLOBAL_HOOKS_DIR, hookType, 'global')
  if (globalHook) {
    locations.push(globalHook)
  }

  // Check project hooks
  const projectDir = path.join(workingDirectory, PROJECT_HOOKS_DIR)
  const projectHook = await findHookScript(projectDir, hookType, 'project')
  if (projectHook) {
    locations.push(projectHook)
  }

  return locations
}

/**
 * Discover all hooks in the system
 *
 * @param workingDirectory - Project working directory
 * @returns Map of hook type to locations
 */
export async function discoverAllHooks(
  workingDirectory: string
): Promise<Map<HookType, HookLocation[]>> {
  const allHooks = new Map<HookType, HookLocation[]>()

  for (const hookType of ALL_HOOK_TYPES) {
    const locations = await discoverHooks(hookType, workingDirectory)
    if (locations.length > 0) {
      allHooks.set(hookType, locations)
    }
  }

  return allHooks
}

// ============================================================================
// Hook Execution
// ============================================================================

/**
 * Execute a single hook script
 *
 * @param location - Hook location to execute
 * @param context - Context to pass via stdin
 * @param config - Execution configuration
 * @returns Hook result or null if error
 */
async function executeHookScript(
  location: HookLocation,
  context: HookContext,
  config: Required<HookConfig>
): Promise<HookResult> {
  return new Promise((resolve) => {
    const input = serializeContext(context as unknown as Record<string, unknown>)

    // Determine how to run the script
    let command: string
    let args: string[]

    const ext = path.extname(location.path).toLowerCase()
    switch (ext) {
      case '.js':
        command = 'node'
        args = [location.path]
        break
      case '.ts':
        command = 'npx'
        args = ['tsx', location.path]
        break
      case '.py':
        command = 'python3'
        args = [location.path]
        break
      case '.rb':
        command = 'ruby'
        args = [location.path]
        break
      case '.ps1':
        command = 'powershell'
        args = ['-File', location.path]
        break
      default:
        // Execute directly (for .sh, extensionless, etc.)
        command = location.path
        args = []
    }

    const proc = spawn(command, args, {
      cwd: config.workingDirectory,
      env: {
        ...process.env,
        ESTELA_HOOK_TYPE: location.type,
        ESTELA_HOOK_SOURCE: location.source,
      },
      stdio: ['pipe', 'pipe', 'pipe'],
      timeout: config.timeout,
    })

    let stdout = ''
    let stderr = ''
    let timedOut = false

    // Set up timeout
    const timeoutId = setTimeout(() => {
      timedOut = true
      proc.kill('SIGKILL')
    }, config.timeout)

    // Collect output
    proc.stdout?.on('data', (data) => {
      stdout += data.toString()
    })

    proc.stderr?.on('data', (data) => {
      stderr += data.toString()
    })

    // Write input and close stdin
    proc.stdin?.write(input)
    proc.stdin?.end()

    // Handle completion
    proc.on('close', (code) => {
      clearTimeout(timeoutId)

      if (timedOut) {
        console.warn(`Hook ${location.type} timed out after ${config.timeout}ms`)
        resolve({
          errorMessage: `Hook timed out after ${config.timeout}ms`,
        })
        return
      }

      if (code !== 0) {
        console.warn(`Hook ${location.type} exited with code ${code}: ${stderr}`)
        // Non-zero exit doesn't block unless the output says to cancel
      }

      try {
        const result = parseHookOutput(stdout, location.type)
        validateHookResult(result, location.type)
        resolve(result)
      } catch (err) {
        console.warn(`Failed to parse hook ${location.type} output: ${err}`)
        resolve({
          errorMessage: `Hook output error: ${err instanceof Error ? err.message : String(err)}`,
        })
      }
    })

    // Handle spawn errors
    proc.on('error', (err) => {
      clearTimeout(timeoutId)
      console.warn(`Failed to execute hook ${location.type}: ${err.message}`)
      resolve({
        errorMessage: `Hook execution failed: ${err.message}`,
      })
    })
  })
}

// ============================================================================
// HookRunner Class
// ============================================================================

/**
 * Manages hook discovery and execution
 */
export class HookRunner {
  private workingDirectory: string
  private config: Required<HookConfig>
  private hookCache: Map<HookType, HookLocation[]> = new Map()
  private listeners: Set<HookEventListener> = new Set()
  private initialized = false

  constructor(workingDirectory: string, config: Partial<HookConfig> = {}) {
    this.workingDirectory = workingDirectory
    this.config = {
      timeout: config.timeout ?? 30_000,
      continueOnError: config.continueOnError ?? true,
      workingDirectory: config.workingDirectory ?? workingDirectory,
    }
  }

  /**
   * Initialize hook discovery (call once at startup)
   */
  async initialize(): Promise<void> {
    if (this.initialized) return

    this.hookCache = await discoverAllHooks(this.workingDirectory)
    this.initialized = true

    // Emit discovery events
    for (const [hookType, locations] of Array.from(this.hookCache.entries())) {
      for (const location of locations) {
        this.emit({
          type: 'hook:discovered',
          hookType,
          hookPath: location.path,
        })
      }
    }
  }

  /**
   * Add event listener
   */
  on(listener: HookEventListener): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  /**
   * Emit hook event
   */
  private emit(event: HookEvent): void {
    for (const listener of Array.from(this.listeners)) {
      try {
        listener(event)
      } catch (err) {
        console.warn('Hook event listener error:', err)
      }
    }
  }

  /**
   * Run hooks for a given type
   *
   * @param hookType - The type of hook to run
   * @param context - Context to pass to hooks
   * @returns Merged result from all hooks
   */
  async run<T extends HookType>(hookType: T, context: HookContext): Promise<HookResult> {
    // Auto-initialize if needed
    if (!this.initialized) {
      await this.initialize()
    }

    const locations = this.hookCache.get(hookType) ?? []
    if (locations.length === 0) {
      return {} // No hooks registered
    }

    const results: HookResult[] = []

    for (const location of locations) {
      const startTime = Date.now()

      this.emit({
        type: 'hook:executing',
        hookType,
        hookPath: location.path,
      })

      try {
        const result = await executeHookScript(location, context, this.config)
        const durationMs = Date.now() - startTime

        this.emit({
          type: 'hook:completed',
          hookType,
          hookPath: location.path,
          result,
          durationMs,
        })

        results.push(result)

        // If hook cancels, stop processing more hooks
        if (result.cancel) {
          break
        }
      } catch (err) {
        const error = err instanceof Error ? err : new Error(String(err))

        this.emit({
          type: 'hook:failed',
          hookType,
          hookPath: location.path,
          error,
        })

        if (!this.config.continueOnError) {
          throw error
        }
      }
    }

    return mergeHookResults(results)
  }

  /**
   * Check if any hooks are registered for a type
   */
  hasHooks(hookType: HookType): boolean {
    return (this.hookCache.get(hookType)?.length ?? 0) > 0
  }

  /**
   * Get all registered hooks
   */
  getRegisteredHooks(): Map<HookType, HookLocation[]> {
    return new Map(this.hookCache)
  }

  /**
   * Refresh hook discovery (call if hooks change during runtime)
   */
  async refresh(): Promise<void> {
    this.initialized = false
    this.hookCache.clear()
    await this.initialize()
  }

  /**
   * Update configuration
   */
  setConfig(config: Partial<HookConfig>): void {
    this.config = {
      ...this.config,
      ...config,
    }
  }
}

// ============================================================================
// Singleton Instance
// ============================================================================

let defaultRunner: HookRunner | null = null

/**
 * Get or create the default HookRunner instance
 */
export function getHookRunner(workingDirectory?: string): HookRunner {
  if (!defaultRunner) {
    defaultRunner = new HookRunner(workingDirectory ?? process.cwd())
  }
  return defaultRunner
}

/**
 * Reset the default HookRunner (for testing)
 */
export function resetHookRunner(): void {
  defaultRunner = null
}

// ============================================================================
// Convenience Function
// ============================================================================

/**
 * Run hooks for a given type using the default runner
 *
 * @param hookType - The type of hook to run
 * @param context - Context to pass to hooks
 * @param workingDirectory - Working directory (uses cwd if not provided)
 * @returns Hook result
 */
export async function runHook<T extends HookType>(
  hookType: T,
  context: HookContext,
  workingDirectory?: string
): Promise<HookResult> {
  const runner = getHookRunner(workingDirectory)
  return runner.run(hookType, context)
}
