/**
 * Core Bridge
 *
 * Central initialization for core engine singletons.
 * Connects the frontend to the well-tested core modules:
 * SettingsManager, ContextTracker, WorkerRegistry, MemoryManager.
 */

import {
  BusMessageType,
  type Compactor,
  type ContextTracker,
  createCompactor,
  createContextTracker,
  createDefaultRegistry,
  createMemoryManager,
  getMessageBus,
  getSettingsManager,
  type MemoryManager,
  type MessageBus,
  type SettingsManager,
  type ToolConfirmationRequest,
  type WorkerRegistry,
} from '@ava/core'
import { logInfo, logWarn } from './logger'

// ============================================================================
// Singleton State
// ============================================================================

let _settings: SettingsManager | null = null
let _tracker: ContextTracker | null = null
let _compactor: Compactor | null = null
let _registry: WorkerRegistry | null = null
let _memory: MemoryManager | null = null
let _bus: MessageBus | null = null

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

/** Get the core MessageBus (null if not initialized) */
export function getCoreBus(): MessageBus | null {
  return _bus
}

/**
 * Subscribe to tool approval requests from the message bus.
 * Returns an unsubscribe function.
 */
export function subscribeToolApproval(
  handler: (request: ToolConfirmationRequest) => void
): () => void {
  if (!_bus) return () => {}
  return _bus.subscribe(BusMessageType.TOOL_CONFIRMATION_REQUEST, handler)
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
  logInfo('core', 'Init core bridge', {
    contextLimit: opts.contextLimit ?? 200_000,
    hasOpenAIApiKey: !!opts.openAIApiKey,
  })
  // Settings — use the core singleton
  _settings = getSettingsManager()

  // Message bus — connects tool execution to UI approval
  _bus = getMessageBus()

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
      logWarn('core', 'Memory init failed')
      _memory = null
    }
  }

  return () => {
    _memory?.dispose()
    _memory = null
    _bus = null
    _compactor = null
    _tracker = null
    _registry = null
    _settings = null
    logInfo('core', 'Core bridge disposed')
  }
}
