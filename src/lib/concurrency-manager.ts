/**
 * Delta9 Provider Concurrency Manager
 *
 * Manages per-provider API concurrency limits with:
 * - Per-provider/model slot limits (e.g., Anthropic: 4, OpenAI: 3)
 * - Request queuing with timeout
 * - Slot acquisition/release with automatic cleanup
 * - Integration with model-fallback for intelligent provider selection
 *
 * Pattern from: oh-my-opencode ConcurrencyManager
 */

import { getNamedLogger } from './logger.js'

const log = getNamedLogger('concurrency')

// =============================================================================
// Types
// =============================================================================

export interface ConcurrencyConfig {
  /** Per-provider concurrency limits */
  limits: Record<string, number>
  /** Default limit for unknown providers */
  defaultLimit: number
  /** Queue timeout in ms (default: 30000) */
  queueTimeout: number
}

export interface ConcurrencySlot {
  /** Provider name (e.g., 'anthropic', 'openai') */
  provider: string
  /** Model ID (e.g., 'claude-sonnet-4') */
  model: string
  /** Timestamp when slot was acquired */
  acquiredAt: number
  /** Session ID that owns this slot */
  sessionId: string
  /** Unique slot ID for tracking */
  slotId: string
}

export interface ConcurrencyStatus {
  /** Provider name */
  provider: string
  /** Number of active slots */
  active: number
  /** Number of queued requests */
  queued: number
  /** Maximum allowed concurrent requests */
  limit: number
}

interface QueueEntry {
  resolve: (releaser: () => void) => void
  reject: (error: Error) => void
  settled: boolean
  timeoutId: ReturnType<typeof setTimeout>
  provider: string
  model: string
  sessionId: string
}

// =============================================================================
// Constants
// =============================================================================

/** Default concurrency configuration */
const DEFAULT_CONFIG: ConcurrencyConfig = {
  limits: {
    anthropic: 4, // Anthropic allows 4 concurrent requests per tier
    openai: 5, // OpenAI allows 5 concurrent (varies by tier)
    google: 5, // Google Gemini
    deepseek: 3, // DeepSeek (conservative)
  },
  defaultLimit: 2,
  queueTimeout: 30000, // 30 seconds
}

// =============================================================================
// Concurrency Manager
// =============================================================================

export class ProviderConcurrencyManager {
  private config: ConcurrencyConfig
  private slots: Map<string, ConcurrencySlot[]> = new Map()
  private queues: Map<string, QueueEntry[]> = new Map()
  private slotCounter = 0

  constructor(config?: Partial<ConcurrencyConfig>) {
    this.config = {
      ...DEFAULT_CONFIG,
      ...config,
      limits: { ...DEFAULT_CONFIG.limits, ...config?.limits },
    }
  }

  // ===========================================================================
  // Configuration
  // ===========================================================================

  /**
   * Get the concurrency limit for a provider
   */
  getLimit(provider: string): number {
    return this.config.limits[provider] ?? this.config.defaultLimit
  }

  /**
   * Update concurrency limit for a provider
   */
  setLimit(provider: string, limit: number): void {
    this.config.limits[provider] = limit
    log.debug(`Set concurrency limit for ${provider} to ${limit}`)
  }

  /**
   * Get the queue timeout
   */
  getQueueTimeout(): number {
    return this.config.queueTimeout
  }

  // ===========================================================================
  // Slot Management
  // ===========================================================================

  /**
   * Extract provider from model string (e.g., 'anthropic/claude-sonnet-4' -> 'anthropic')
   */
  extractProvider(model: string): string {
    const parts = model.split('/')
    return parts[0] || 'unknown'
  }

  /**
   * Get current slot count for a provider
   */
  getActiveCount(provider: string): number {
    return this.slots.get(provider)?.length ?? 0
  }

  /**
   * Get current queue length for a provider
   */
  getQueuedCount(provider: string): number {
    return this.queues.get(provider)?.length ?? 0
  }

  /**
   * Check if a slot is available for a provider
   */
  hasAvailableSlot(provider: string): boolean {
    const active = this.getActiveCount(provider)
    const limit = this.getLimit(provider)
    return active < limit
  }

  /**
   * Acquire a concurrency slot for a provider
   *
   * Returns a release function that MUST be called when done.
   * Blocks until a slot is available or timeout is reached.
   *
   * @throws Error if queue timeout is reached
   */
  async acquire(model: string, sessionId: string): Promise<() => void> {
    const provider = this.extractProvider(model)
    const limit = this.getLimit(provider)
    const active = this.getActiveCount(provider)

    log.debug(`Acquiring slot for ${provider}/${model} (active: ${active}/${limit})`)

    // If slot available, acquire immediately
    if (active < limit) {
      return this.createSlot(provider, model, sessionId)
    }

    // Otherwise, queue the request
    return this.enqueue(provider, model, sessionId)
  }

  /**
   * Try to acquire a slot without waiting
   *
   * Returns null if no slot available.
   */
  tryAcquire(model: string, sessionId: string): (() => void) | null {
    const provider = this.extractProvider(model)

    if (!this.hasAvailableSlot(provider)) {
      return null
    }

    return this.createSlot(provider, model, sessionId)
  }

  /**
   * Release a specific slot by session ID
   *
   * Automatically called by the releaser function, but can be called
   * directly for cleanup (e.g., on session termination).
   */
  releaseBySession(sessionId: string): number {
    let released = 0

    for (const [provider, providerSlots] of this.slots) {
      const before = providerSlots.length
      const remaining = providerSlots.filter((s) => s.sessionId !== sessionId)
      this.slots.set(provider, remaining)
      const releasedCount = before - remaining.length

      if (releasedCount > 0) {
        released += releasedCount
        log.debug(`Released ${releasedCount} slots for session ${sessionId} from ${provider}`)
        this.processQueue(provider)
      }
    }

    return released
  }

  /**
   * Clear all slots and queues (for shutdown)
   */
  clear(): void {
    // Reject all queued requests
    for (const [provider, queue] of this.queues) {
      for (const entry of queue) {
        if (!entry.settled) {
          entry.settled = true
          clearTimeout(entry.timeoutId)
          entry.reject(new Error('Concurrency manager cleared'))
        }
      }
      this.queues.delete(provider)
    }

    // Clear all slots
    this.slots.clear()

    log.info('Concurrency manager cleared')
  }

  // ===========================================================================
  // Status
  // ===========================================================================

  /**
   * Get status for all providers
   */
  getStatus(): ConcurrencyStatus[] {
    const providers = new Set([
      ...this.slots.keys(),
      ...this.queues.keys(),
      ...Object.keys(this.config.limits),
    ])

    return Array.from(providers).map((provider) => ({
      provider,
      active: this.getActiveCount(provider),
      queued: this.getQueuedCount(provider),
      limit: this.getLimit(provider),
    }))
  }

  /**
   * Get status for a specific provider
   */
  getProviderStatus(provider: string): ConcurrencyStatus {
    return {
      provider,
      active: this.getActiveCount(provider),
      queued: this.getQueuedCount(provider),
      limit: this.getLimit(provider),
    }
  }

  /**
   * Get all active slots (for debugging/monitoring)
   */
  getActiveSlots(): ConcurrencySlot[] {
    const allSlots: ConcurrencySlot[] = []
    for (const slots of this.slots.values()) {
      allSlots.push(...slots)
    }
    return allSlots
  }

  // ===========================================================================
  // Internal Methods
  // ===========================================================================

  /**
   * Create a new slot and return the releaser function
   */
  private createSlot(provider: string, model: string, sessionId: string): () => void {
    const slotId = `slot_${++this.slotCounter}`

    const slot: ConcurrencySlot = {
      provider,
      model,
      acquiredAt: Date.now(),
      sessionId,
      slotId,
    }

    // Add to provider's slots
    const providerSlots = this.slots.get(provider) ?? []
    providerSlots.push(slot)
    this.slots.set(provider, providerSlots)

    log.debug(`Created slot ${slotId} for ${provider}/${model} (session: ${sessionId})`)

    // Return releaser function
    let released = false
    return () => {
      if (released) return
      released = true
      this.releaseSlot(provider, slotId)
    }
  }

  /**
   * Release a slot by ID
   */
  private releaseSlot(provider: string, slotId: string): void {
    const providerSlots = this.slots.get(provider)
    if (!providerSlots) return

    const index = providerSlots.findIndex((s) => s.slotId === slotId)
    if (index === -1) return

    providerSlots.splice(index, 1)
    log.debug(`Released slot ${slotId} for ${provider}`)

    // Process waiting queue
    this.processQueue(provider)
  }

  /**
   * Add request to queue
   */
  private enqueue(provider: string, model: string, sessionId: string): Promise<() => void> {
    return new Promise((resolve, reject) => {
      const entry: QueueEntry = {
        resolve,
        reject,
        settled: false,
        provider,
        model,
        sessionId,
        timeoutId: setTimeout(() => {
          if (!entry.settled) {
            entry.settled = true
            this.removeFromQueue(provider, entry)
            reject(
              new Error(
                `Concurrency queue timeout (${this.config.queueTimeout}ms) for provider ${provider}`
              )
            )
          }
        }, this.config.queueTimeout),
      }

      const queue = this.queues.get(provider) ?? []
      queue.push(entry)
      this.queues.set(provider, queue)

      log.debug(`Queued request for ${provider}/${model} (queue length: ${queue.length})`)
    })
  }

  /**
   * Remove entry from queue
   */
  private removeFromQueue(provider: string, entry: QueueEntry): void {
    const queue = this.queues.get(provider)
    if (!queue) return

    const index = queue.indexOf(entry)
    if (index !== -1) {
      queue.splice(index, 1)
    }
  }

  /**
   * Process queue when a slot becomes available
   */
  private processQueue(provider: string): void {
    const queue = this.queues.get(provider)
    if (!queue || queue.length === 0) return

    const limit = this.getLimit(provider)
    const active = this.getActiveCount(provider)

    if (active >= limit) return

    // Get next entry from queue
    const entry = queue.shift()
    if (!entry || entry.settled) {
      // Entry was already settled (timeout), try next
      this.processQueue(provider)
      return
    }

    // Mark as settled and clear timeout
    entry.settled = true
    clearTimeout(entry.timeoutId)

    // Create slot and resolve
    const releaser = this.createSlot(provider, entry.model, entry.sessionId)
    entry.resolve(releaser)

    log.debug(`Dequeued request for ${provider}/${entry.model}`)
  }
}

// =============================================================================
// Singleton Instance
// =============================================================================

let instance: ProviderConcurrencyManager | null = null

/**
 * Get the global concurrency manager instance
 */
export function getConcurrencyManager(config?: Partial<ConcurrencyConfig>): ProviderConcurrencyManager {
  if (!instance) {
    instance = new ProviderConcurrencyManager(config)
    log.info('Provider concurrency manager initialized')
  }
  return instance
}

/**
 * Clear the global instance (for testing)
 */
export function clearConcurrencyManager(): void {
  if (instance) {
    instance.clear()
    instance = null
  }
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Execute a function with automatic slot management
 *
 * Acquires a slot, executes the function, then releases the slot.
 * Handles errors and ensures slot is always released.
 */
export async function withConcurrencySlot<T>(
  model: string,
  sessionId: string,
  fn: () => Promise<T>,
  manager?: ProviderConcurrencyManager
): Promise<T> {
  const mgr = manager ?? getConcurrencyManager()
  const release = await mgr.acquire(model, sessionId)

  try {
    return await fn()
  } finally {
    release()
  }
}

/**
 * Describe concurrency status in human-readable format
 */
export function describeConcurrencyStatus(status: ConcurrencyStatus[]): string {
  if (status.length === 0) {
    return 'No active providers'
  }

  const lines = ['Provider Concurrency Status:']

  for (const s of status) {
    const utilization = s.limit > 0 ? Math.round((s.active / s.limit) * 100) : 0
    lines.push(`  ${s.provider}: ${s.active}/${s.limit} (${utilization}% utilized, ${s.queued} queued)`)
  }

  return lines.join('\n')
}
