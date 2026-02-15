/**
 * Agent Metrics
 *
 * Collects per-session execution metrics from agent events.
 * Tracks turns, tokens, tool usage, errors, and recoveries.
 */

import type { AgentEvent } from './types.js'

// ============================================================================
// Types
// ============================================================================

/** Per-session agent metrics */
export interface AgentMetrics {
  /** Session ID */
  sessionId: string
  /** Total conversation turns */
  totalTurns: number
  /** Total input tokens consumed */
  totalTokensIn: number
  /** Total output tokens produced */
  totalTokensOut: number
  /** Total execution time in ms */
  totalDurationMs: number
  /** Tool call counts by tool name */
  toolCalls: Record<string, number>
  /** Total errors encountered */
  errors: number
  /** Total recovery attempts */
  recoveries: number
  /** Timestamp when session started */
  startedAt: number
  /** Timestamp when session completed */
  completedAt?: number
}

// ============================================================================
// MetricsCollector
// ============================================================================

/**
 * Collects and aggregates agent execution metrics per session.
 *
 * Usage:
 * ```ts
 * const collector = new MetricsCollector()
 * // Wire into agent event callback
 * collector.record('session-1', event)
 * // Later, retrieve metrics
 * const metrics = collector.getMetrics('session-1')
 * ```
 */
export class MetricsCollector {
  private sessions = new Map<string, AgentMetrics>()

  /**
   * Record an agent event for a session
   */
  record(sessionId: string, event: AgentEvent): void {
    let metrics = this.sessions.get(sessionId)
    if (!metrics) {
      metrics = createEmptyMetrics(sessionId)
      this.sessions.set(sessionId, metrics)
    }

    switch (event.type) {
      case 'agent:start':
        metrics.startedAt = event.timestamp
        break

      case 'agent:finish':
        metrics.completedAt = event.timestamp
        if (metrics.startedAt > 0) {
          metrics.totalDurationMs = event.timestamp - metrics.startedAt
        }
        break

      case 'turn:start':
        metrics.totalTurns++
        break

      case 'turn:finish':
        if (event.toolCalls) {
          for (const tc of event.toolCalls) {
            metrics.toolCalls[tc.name] = (metrics.toolCalls[tc.name] ?? 0) + 1
          }
        }
        if (event.tokensIn !== undefined) {
          metrics.totalTokensIn += event.tokensIn
        }
        if (event.tokensOut !== undefined) {
          metrics.totalTokensOut += event.tokensOut
        }
        break

      case 'tool:error':
      case 'error':
        metrics.errors++
        break

      case 'recovery:start':
        metrics.recoveries++
        break
    }
  }

  /**
   * Get metrics for a session
   */
  getMetrics(sessionId: string): AgentMetrics | undefined {
    return this.sessions.get(sessionId)
  }

  /**
   * Get all session metrics
   */
  getAllMetrics(): AgentMetrics[] {
    return Array.from(this.sessions.values())
  }

  /**
   * Reset metrics for a session
   */
  reset(sessionId: string): void {
    this.sessions.delete(sessionId)
  }

  /**
   * Export all metrics (returns a copy)
   */
  exportMetrics(): AgentMetrics[] {
    return this.getAllMetrics().map((m) => ({ ...m, toolCalls: { ...m.toolCalls } }))
  }

  /**
   * Clear all session metrics
   */
  clear(): void {
    this.sessions.clear()
  }

  /**
   * Get number of tracked sessions
   */
  get sessionCount(): number {
    return this.sessions.size
  }
}

// ============================================================================
// Helpers
// ============================================================================

/** Create empty metrics for a new session */
function createEmptyMetrics(sessionId: string): AgentMetrics {
  return {
    sessionId,
    totalTurns: 0,
    totalTokensIn: 0,
    totalTokensOut: 0,
    totalDurationMs: 0,
    toolCalls: {},
    errors: 0,
    recoveries: 0,
    startedAt: Date.now(),
  }
}

// ============================================================================
// Singleton
// ============================================================================

let defaultCollector: MetricsCollector | null = null

/** Get or create the default MetricsCollector */
export function getMetricsCollector(): MetricsCollector {
  if (!defaultCollector) {
    defaultCollector = new MetricsCollector()
  }
  return defaultCollector
}

/** Create a new MetricsCollector */
export function createMetricsCollector(): MetricsCollector {
  return new MetricsCollector()
}

/** Set the default collector (for testing) */
export function setMetricsCollector(collector: MetricsCollector | null): void {
  defaultCollector = collector
}
