/**
 * Delta9 Agent Fallback Manager
 *
 * Implements circuit breaker pattern for agent failure tracking.
 * When an agent fails N times, it triggers auto-fallback to the next agent in chain.
 *
 * Circuit States:
 * - CLOSED: Normal operation, agent is available
 * - OPEN: Too many failures, agent is unavailable
 * - HALF_OPEN: Testing if agent has recovered
 *
 * BUG-16 fix: Prevents repeated failures by tracking agent health.
 */

import { getNamedLogger } from './logger.js'
import { loadConfig } from './config.js'

const log = getNamedLogger('agent-fallback')

// =============================================================================
// Configuration
// =============================================================================

/** Default failure threshold before circuit opens */
const DEFAULT_FAILURE_THRESHOLD = 3

/** Default cooldown period in ms before trying half-open */
const DEFAULT_COOLDOWN_MS = 60_000 // 1 minute

/** Max depth of fallback chain to traverse */
const MAX_FALLBACK_DEPTH = 2

// =============================================================================
// Types
// =============================================================================

export type CircuitState = 'closed' | 'open' | 'half_open'

export interface AgentHealth {
  /** Agent identifier */
  agent: string
  /** Current circuit state */
  state: CircuitState
  /** Consecutive failure count */
  failureCount: number
  /** Consecutive success count (in half_open state) */
  successCount: number
  /** Last failure timestamp */
  lastFailure?: number
  /** Last success timestamp */
  lastSuccess?: number
  /** When circuit opened */
  circuitOpenedAt?: number
}

export interface FallbackResult {
  /** Agent to use */
  agent: string
  /** Whether this is a fallback from requested agent */
  isFallback: boolean
  /** Original requested agent (if fallback) */
  originalAgent?: string
  /** Reason for fallback */
  reason?: string
}

// =============================================================================
// Agent Fallback Chains
// =============================================================================

/**
 * Default fallback chains for agents.
 * Loaded from config when available, otherwise uses these defaults.
 */
const DEFAULT_FALLBACK_CHAINS: Record<string, string[]> = {
  // Support agents (7 agents - SPECTRE/optics removed, merged into FACADE)
  uiOps: ['operator'],
  scout: ['operator'],
  intel: ['operator'],
  strategist: ['operator'],
  patcher: ['operator'],
  qa: ['validator', 'operator'],
  scribe: ['operator'],
  // Core agents
  operator: [], // No fallback for operator
  validator: ['operator'],
  // Council Strategic Advisors (6 advisors - Prism removed, AEGIS/RAZOR/ORACLE added)
  cipher: ['vector', 'operator'],
  vector: ['cipher', 'operator'],
  apex: ['aegis', 'operator'],
  aegis: ['apex', 'operator'],
  razor: ['oracle', 'operator'],
  oracle: ['razor', 'operator'],
}

// =============================================================================
// Agent Fallback Manager
// =============================================================================

export class AgentFallbackManager {
  private healthMap: Map<string, AgentHealth> = new Map()
  private failureThreshold: number
  private cooldownMs: number
  private fallbackChains: Record<string, string[]>

  constructor(
    options: {
      failureThreshold?: number
      cooldownMs?: number
      fallbackChains?: Record<string, string[]>
    } = {}
  ) {
    this.failureThreshold = options.failureThreshold ?? DEFAULT_FAILURE_THRESHOLD
    this.cooldownMs = options.cooldownMs ?? DEFAULT_COOLDOWN_MS
    this.fallbackChains = options.fallbackChains ?? DEFAULT_FALLBACK_CHAINS
  }

  /**
   * Get or create health record for an agent
   */
  private getHealth(agent: string): AgentHealth {
    let health = this.healthMap.get(agent)
    if (!health) {
      health = {
        agent,
        state: 'closed',
        failureCount: 0,
        successCount: 0,
      }
      this.healthMap.set(agent, health)
    }
    return health
  }

  /**
   * Record a failure for an agent
   */
  recordFailure(agent: string, error?: string): void {
    const health = this.getHealth(agent)
    const now = Date.now()

    health.failureCount++
    health.successCount = 0
    health.lastFailure = now

    log.debug(`Agent ${agent} failed (${health.failureCount}/${this.failureThreshold})`, {
      error,
      state: health.state,
    })

    // Check if should open circuit
    if (health.failureCount >= this.failureThreshold && health.state === 'closed') {
      health.state = 'open'
      health.circuitOpenedAt = now
      log.warn(`Circuit OPENED for agent ${agent} after ${health.failureCount} failures`)
    }

    // If half_open and failed, reopen circuit
    if (health.state === 'half_open') {
      health.state = 'open'
      health.circuitOpenedAt = now
      log.warn(`Circuit REOPENED for agent ${agent} after half-open failure`)
    }
  }

  /**
   * Record a success for an agent
   */
  recordSuccess(agent: string): void {
    const health = this.getHealth(agent)
    const now = Date.now()

    health.successCount++
    health.lastSuccess = now

    // If half_open and succeeded, close circuit
    if (health.state === 'half_open') {
      health.state = 'closed'
      health.failureCount = 0
      log.info(`Circuit CLOSED for agent ${agent} after successful recovery`)
    }

    // Decay failure count on success (gradual recovery)
    if (health.state === 'closed' && health.failureCount > 0) {
      health.failureCount = Math.max(0, health.failureCount - 1)
    }
  }

  /**
   * Check if agent is available
   */
  isAgentAvailable(agent: string): boolean {
    const health = this.getHealth(agent)
    const now = Date.now()

    // Closed circuit = available
    if (health.state === 'closed') {
      return true
    }

    // Half-open = available (testing recovery)
    if (health.state === 'half_open') {
      return true
    }

    // Open circuit - check if cooldown has passed
    if (health.state === 'open' && health.circuitOpenedAt) {
      const elapsed = now - health.circuitOpenedAt
      if (elapsed >= this.cooldownMs) {
        // Move to half-open state
        health.state = 'half_open'
        log.info(`Circuit HALF-OPEN for agent ${agent} after ${elapsed}ms cooldown`)
        return true
      }
    }

    return false
  }

  /**
   * Get the best available agent, falling back if necessary
   */
  getBestAgent(requestedAgent: string, depth: number = 0): FallbackResult {
    // Check if requested agent is available
    if (this.isAgentAvailable(requestedAgent)) {
      return {
        agent: requestedAgent,
        isFallback: false,
      }
    }

    // Max depth reached, return requested anyway (will likely fail)
    if (depth >= MAX_FALLBACK_DEPTH) {
      log.warn(`Max fallback depth reached for ${requestedAgent}, using anyway`)
      return {
        agent: requestedAgent,
        isFallback: false,
        reason: 'Max fallback depth reached',
      }
    }

    // Get fallback chain
    const chain = this.fallbackChains[requestedAgent] ?? []

    // Try each fallback in order
    for (const fallback of chain) {
      const result = this.getBestAgent(fallback, depth + 1)
      if (result.isFallback || this.isAgentAvailable(fallback)) {
        log.info(`Falling back from ${requestedAgent} to ${result.agent}`)
        return {
          agent: result.agent,
          isFallback: true,
          originalAgent: requestedAgent,
          reason: `Agent ${requestedAgent} circuit open, using fallback`,
        }
      }
    }

    // No fallbacks available, return requested
    log.warn(`No fallbacks available for ${requestedAgent}, using anyway`)
    return {
      agent: requestedAgent,
      isFallback: false,
      reason: 'No fallbacks available',
    }
  }

  /**
   * Get health status for all tracked agents
   */
  getHealthStatus(): AgentHealth[] {
    return Array.from(this.healthMap.values())
  }

  /**
   * Get health status for a specific agent
   */
  getAgentHealth(agent: string): AgentHealth | undefined {
    return this.healthMap.get(agent)
  }

  /**
   * Reset health for an agent (manual recovery)
   */
  resetAgent(agent: string): void {
    const health = this.getHealth(agent)
    health.state = 'closed'
    health.failureCount = 0
    health.successCount = 0
    health.circuitOpenedAt = undefined
    log.info(`Agent ${agent} health reset manually`)
  }

  /**
   * Reset all agent health
   */
  resetAll(): void {
    this.healthMap.clear()
    log.info('All agent health reset')
  }
}

// =============================================================================
// Singleton Instance
// =============================================================================

let instance: AgentFallbackManager | null = null

/**
 * Get the global fallback manager instance
 */
export function getAgentFallbackManager(cwd?: string): AgentFallbackManager {
  if (!instance) {
    // Try to load config for fallback chains
    let fallbackChains = DEFAULT_FALLBACK_CHAINS

    if (cwd) {
      try {
        const config = loadConfig(cwd)
        // Build fallback chains from config if available
        const chains: Record<string, string[]> = {}

        // Support agents
        if (config.support) {
          chains.uiOps = config.support.uiOps?.fallbacks?.map(normalizeFallback) ?? ['operator']
          chains.scout = config.support.scout?.fallbacks?.map(normalizeFallback) ?? ['operator']
          chains.intel = config.support.intel?.fallbacks?.map(normalizeFallback) ?? ['operator']
          chains.strategist = config.support.strategist?.fallbacks?.map(normalizeFallback) ?? [
            'operator',
          ]
          chains.qa = config.support.qa?.fallbacks?.map(normalizeFallback) ?? [
            'validator',
            'operator',
          ]
          chains.scribe = config.support.scribe?.fallbacks?.map(normalizeFallback) ?? ['operator']
        }

        // Core agents (3-tier system uses tier2 as default operator)
        if (config.operators) {
          chains.operator = config.operators.tier2Fallbacks?.map(normalizeFallback) ?? []
        }
        if (config.validator) {
          chains.validator = config.validator.fallbacks?.map(normalizeFallback) ?? ['operator']
        }

        fallbackChains = { ...DEFAULT_FALLBACK_CHAINS, ...chains }
      } catch {
        // Use defaults if config fails to load
      }
    }

    instance = new AgentFallbackManager({ fallbackChains })
  }
  return instance
}

/**
 * Normalize model-style fallback to agent name
 * e.g., "anthropic/claude-sonnet-4-5" -> "operator"
 */
function normalizeFallback(fallback: string): string {
  // If it's a model path, default to operator
  if (fallback.includes('/')) {
    return 'operator'
  }
  return fallback
}

/**
 * Reset the singleton instance (for testing)
 */
export function resetAgentFallbackManager(): void {
  instance = null
}
