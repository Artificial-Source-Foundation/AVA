/**
 * Delta9 Process Cleanup Manager
 *
 * Centralized graceful shutdown handling with:
 * - SIGINT/SIGTERM/beforeExit handlers
 * - Priority-ordered cleanup execution
 * - Timeout protection
 * - Single instance coordination
 *
 * Pattern from: swarm-plugin graceful shutdown
 */

import { getNamedLogger } from './logger.js'

const log = getNamedLogger('cleanup')

// =============================================================================
// Types
// =============================================================================

export interface CleanupHandler {
  /** Unique name for this handler */
  name: string
  /** Priority (lower = runs first) */
  priority: number
  /** Cleanup function */
  handler: () => Promise<void> | void
  /** Timeout in ms (default: 5000) */
  timeout?: number
}

export interface CleanupConfig {
  /** Maximum time for all cleanup (ms) */
  totalTimeout: number
  /** Default handler timeout (ms) */
  defaultHandlerTimeout: number
  /** Whether to exit after cleanup on signals */
  exitOnSignal: boolean
  /** Exit code to use */
  exitCode: number
}

type CleanupSignal = 'SIGINT' | 'SIGTERM' | 'SIGBREAK' | 'beforeExit' | 'exit'

// =============================================================================
// Constants
// =============================================================================

const DEFAULT_CONFIG: CleanupConfig = {
  totalTimeout: 10000, // 10 seconds total
  defaultHandlerTimeout: 5000, // 5 seconds per handler
  exitOnSignal: true,
  exitCode: 0,
}

// =============================================================================
// Process Cleanup Manager
// =============================================================================

export class ProcessCleanupManager {
  private static instance: ProcessCleanupManager | null = null

  private handlers: Map<string, CleanupHandler> = new Map()
  private config: CleanupConfig
  private isShuttingDown = false
  private signalHandlers: Map<CleanupSignal, () => void> = new Map()
  private registered = false

  constructor(config?: Partial<CleanupConfig>) {
    this.config = { ...DEFAULT_CONFIG, ...config }
  }

  /**
   * Get the singleton instance
   */
  static getInstance(config?: Partial<CleanupConfig>): ProcessCleanupManager {
    if (!ProcessCleanupManager.instance) {
      ProcessCleanupManager.instance = new ProcessCleanupManager(config)
    }
    return ProcessCleanupManager.instance
  }

  /**
   * Clear the singleton instance (for testing)
   */
  static clearInstance(): void {
    if (ProcessCleanupManager.instance) {
      ProcessCleanupManager.instance.unregisterSignals()
      ProcessCleanupManager.instance = null
    }
  }

  // ===========================================================================
  // Handler Registration
  // ===========================================================================

  /**
   * Register a cleanup handler
   */
  register(handler: CleanupHandler): void {
    if (this.handlers.has(handler.name)) {
      log.warn(`Replacing existing cleanup handler: ${handler.name}`)
    }
    this.handlers.set(handler.name, {
      ...handler,
      timeout: handler.timeout ?? this.config.defaultHandlerTimeout,
    })
    log.debug(`Registered cleanup handler: ${handler.name} (priority: ${handler.priority})`)

    // Auto-register signals on first handler
    if (!this.registered) {
      this.registerSignals()
    }
  }

  /**
   * Unregister a cleanup handler
   */
  unregister(name: string): boolean {
    const removed = this.handlers.delete(name)
    if (removed) {
      log.debug(`Unregistered cleanup handler: ${name}`)
    }
    return removed
  }

  /**
   * Check if a handler is registered
   */
  has(name: string): boolean {
    return this.handlers.has(name)
  }

  /**
   * Get all registered handler names
   */
  getHandlerNames(): string[] {
    return Array.from(this.handlers.keys())
  }

  // ===========================================================================
  // Shutdown
  // ===========================================================================

  /**
   * Execute cleanup handlers in priority order
   */
  async shutdown(reason: string = 'manual'): Promise<void> {
    if (this.isShuttingDown) {
      log.warn('Shutdown already in progress')
      return
    }
    this.isShuttingDown = true

    log.info(`Starting cleanup (reason: ${reason})`)
    const startTime = Date.now()

    // Sort handlers by priority (lower = first)
    const sortedHandlers = Array.from(this.handlers.values()).sort(
      (a, b) => a.priority - b.priority
    )

    // Execute handlers with timeout protection
    for (const handler of sortedHandlers) {
      const elapsed = Date.now() - startTime
      if (elapsed >= this.config.totalTimeout) {
        log.warn(`Total cleanup timeout reached, skipping remaining handlers`)
        break
      }

      try {
        log.debug(`Running cleanup handler: ${handler.name}`)
        const handlerStart = Date.now()

        await Promise.race([
          Promise.resolve(handler.handler()),
          new Promise<never>((_, reject) =>
            setTimeout(
              () => reject(new Error(`Handler timeout: ${handler.name}`)),
              handler.timeout
            )
          ),
        ])

        const handlerDuration = Date.now() - handlerStart
        log.debug(`Cleanup handler ${handler.name} completed in ${handlerDuration}ms`)
      } catch (error) {
        log.error(
          `Cleanup handler ${handler.name} failed: ${error instanceof Error ? error.message : String(error)}`
        )
        // Continue with other handlers
      }
    }

    const totalDuration = Date.now() - startTime
    log.info(`Cleanup completed in ${totalDuration}ms`)
  }

  /**
   * Check if shutdown is in progress
   */
  isShutdown(): boolean {
    return this.isShuttingDown
  }

  /**
   * Reset shutdown state (for testing)
   */
  reset(): void {
    this.isShuttingDown = false
    this.handlers.clear()
  }

  // ===========================================================================
  // Signal Handling
  // ===========================================================================

  /**
   * Register process signal handlers
   */
  private registerSignals(): void {
    if (this.registered) return
    this.registered = true

    const createHandler = (signal: CleanupSignal, shouldExit: boolean) => {
      const handler = () => {
        this.shutdown(signal).finally(() => {
          if (shouldExit && this.config.exitOnSignal) {
            process.exit(this.config.exitCode)
          }
        })
      }
      this.signalHandlers.set(signal, handler)
      return handler
    }

    // Register signal handlers
    const signals: Array<{ signal: CleanupSignal; shouldExit: boolean }> = [
      { signal: 'SIGINT', shouldExit: true },
      { signal: 'SIGTERM', shouldExit: true },
      { signal: 'beforeExit', shouldExit: false },
    ]

    // Add Windows-specific signal
    if (process.platform === 'win32') {
      signals.push({ signal: 'SIGBREAK', shouldExit: true })
    }

    for (const { signal, shouldExit } of signals) {
      const handler = createHandler(signal, shouldExit)
      process.on(signal, handler)
    }

    log.info('Process signal handlers registered')
  }

  /**
   * Unregister process signal handlers
   */
  private unregisterSignals(): void {
    for (const [signal, handler] of this.signalHandlers) {
      process.off(signal, handler)
    }
    this.signalHandlers.clear()
    this.registered = false
    log.debug('Process signal handlers unregistered')
  }
}

// =============================================================================
// Convenience Functions
// =============================================================================

/**
 * Get the global cleanup manager
 */
export function getCleanupManager(config?: Partial<CleanupConfig>): ProcessCleanupManager {
  return ProcessCleanupManager.getInstance(config)
}

/**
 * Register a cleanup handler with the global manager
 */
export function registerCleanup(
  name: string,
  handler: () => Promise<void> | void,
  priority: number = 50,
  timeout?: number
): void {
  getCleanupManager().register({ name, handler, priority, timeout })
}

/**
 * Unregister a cleanup handler from the global manager
 */
export function unregisterCleanup(name: string): boolean {
  return getCleanupManager().unregister(name)
}

/**
 * Trigger global shutdown
 */
export async function shutdown(reason: string = 'manual'): Promise<void> {
  return getCleanupManager().shutdown(reason)
}

// =============================================================================
// Predefined Priority Levels
// =============================================================================

export const CleanupPriority = {
  /** Critical resources (abort active operations) */
  CRITICAL: 10,
  /** Background tasks */
  BACKGROUND: 20,
  /** State persistence */
  STATE: 30,
  /** History/logging */
  LOGGING: 40,
  /** Default priority */
  DEFAULT: 50,
  /** Cleanup resources (remove temp files) */
  CLEANUP: 60,
  /** Final (last resort cleanup) */
  FINAL: 100,
} as const
