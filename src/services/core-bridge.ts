/**
 * Core Bridge
 *
 * Central initialization for core engine singletons.
 * Connects the frontend to the well-tested core modules:
 * SettingsManager, ContextTracker, WorkerRegistry, MemoryManager.
 */

import {
  type Compactor,
  type ContextTracker,
  createCompactor,
  createContextTracker,
  createDefaultRegistry,
  createMemoryManager,
  getSettingsManager,
  type MemoryManager,
  type SettingsManager,
  type WorkerRegistry,
} from '@estela/core'

// ============================================================================
// Singleton State
// ============================================================================

let _settings: SettingsManager | null = null
let _tracker: ContextTracker | null = null
let _compactor: Compactor | null = null
let _registry: WorkerRegistry | null = null
let _memory: MemoryManager | null = null

// ============================================================================
// Getters
// ============================================================================

/** Get the core SettingsManager (null if not initialized) */
export function getCoreSettings(): SettingsManager | null {
  return _settings
}

/** Get the core ContextTracker (null if not initialized) */
export function getCoreTracker(): ContextTracker | null {
  return _tracker
}

/** Get the core Compactor (null if not initialized) */
export function getCoreCompactor(): Compactor | null {
  return _compactor
}

/** Get the core WorkerRegistry (null if not initialized) */
export function getCoreRegistry(): WorkerRegistry | null {
  return _registry
}

/** Get the core MemoryManager (null if not initialized) */
export function getCoreMemory(): MemoryManager | null {
  return _memory
}

// ============================================================================
// Initialization
// ============================================================================

export interface CoreBridgeOptions {
  /** Context window limit in tokens (default: 200_000) */
  contextLimit?: number
  /** OpenAI API key for memory embeddings (optional, memory disabled without it) */
  openAIApiKey?: string
}

/**
 * Initialize all core engine singletons.
 * Returns a cleanup function to dispose resources.
 */
export async function initCoreBridge(opts: CoreBridgeOptions = {}): Promise<() => void> {
  // Settings — use the core singleton
  _settings = getSettingsManager()

  // Context tracking — real token counting via gpt-tokenizer
  _tracker = createContextTracker(opts.contextLimit ?? 200_000)

  // Compactor — auto-compacts conversation when context exceeds threshold
  _compactor = createCompactor(_tracker, 50)

  // Worker registry — 5 built-in workers (coder, tester, reviewer, researcher, debugger)
  _registry = createDefaultRegistry()

  // Memory — requires OpenAI API key for embeddings, graceful degradation
  if (opts.openAIApiKey) {
    try {
      _memory = createMemoryManager({ openAIApiKey: opts.openAIApiKey })
    } catch {
      _memory = null
    }
  }

  return () => {
    _memory?.dispose()
    _memory = null
    _compactor = null
    _tracker = null
    _registry = null
    _settings = null
  }
}
