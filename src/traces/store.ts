/**
 * Delta9 Decision Trace Store
 *
 * Persistent storage for decision traces using JSONL format.
 * Integrates with event system for audit trail.
 */

import { existsSync, mkdirSync, readFileSync, appendFileSync } from 'node:fs'
import { join } from 'node:path'
import { nanoid } from 'nanoid'
import type {
  DecisionTrace,
  TraceQuery,
  TraceResult,
  CreateTraceInput,
  PrecedentChain,
  TraceStats,
  DecisionType,
} from './types.js'
import { DecisionTraceSchema, DecisionTypeSchema } from './types.js'
import { getEventStore } from '../events/store.js'

// =============================================================================
// Types
// =============================================================================

export interface TraceStoreOptions {
  /** Base directory for trace storage */
  baseDir?: string
  /** Maximum traces to keep in memory */
  maxMemoryTraces?: number
}

// =============================================================================
// Trace Store Class
// =============================================================================

export class TraceStore {
  private traces: Map<string, DecisionTrace> = new Map()
  private storePath: string
  private maxMemoryTraces: number
  private loaded = false

  constructor(options: TraceStoreOptions = {}) {
    const baseDir = options.baseDir || process.cwd()
    this.storePath = join(baseDir, '.delta9', 'traces.jsonl')
    this.maxMemoryTraces = options.maxMemoryTraces || 1000
  }

  // ===========================================================================
  // Core Operations
  // ===========================================================================

  /**
   * Record a new decision trace
   */
  record(input: CreateTraceInput): DecisionTrace {
    this.ensureLoaded()

    const trace: DecisionTrace = {
      id: `trace-${nanoid(12)}`,
      type: input.type,
      decision: input.decision,
      reasoning: input.reasoning,
      alternatives: input.alternatives || [],
      confidence: input.confidence,
      precedentIds: input.precedentIds,
      context: input.context,
      timestamp: new Date().toISOString(),
      decidedBy: input.decidedBy,
    }

    // Validate
    DecisionTraceSchema.parse(trace)

    // Store in memory
    this.traces.set(trace.id, trace)

    // Persist to disk
    this.appendTrace(trace)

    // Emit event
    this.emitTraceEvent(trace)

    // Trim memory if needed
    this.trimMemory()

    return trace
  }

  /**
   * Get a trace by ID
   */
  get(id: string): DecisionTrace | null {
    this.ensureLoaded()
    return this.traces.get(id) || null
  }

  /**
   * Query traces with filters
   */
  query(query: TraceQuery): TraceResult {
    this.ensureLoaded()

    let results = Array.from(this.traces.values())

    // Apply filters
    if (query.type) {
      results = results.filter((t) => t.type === query.type)
    }

    if (query.taskId) {
      results = results.filter((t) => t.context?.taskId === query.taskId)
    }

    if (query.missionId) {
      results = results.filter((t) => t.context?.missionId === query.missionId)
    }

    if (query.agentId) {
      results = results.filter((t) => t.context?.agentId === query.agentId)
    }

    if (query.sessionId) {
      results = results.filter((t) => t.context?.sessionId === query.sessionId)
    }

    if (query.since) {
      const sinceTime = new Date(query.since).getTime()
      results = results.filter((t) => new Date(t.timestamp).getTime() >= sinceTime)
    }

    if (query.until) {
      const untilTime = new Date(query.until).getTime()
      results = results.filter((t) => new Date(t.timestamp).getTime() <= untilTime)
    }

    if (query.search) {
      const searchLower = query.search.toLowerCase()
      results = results.filter(
        (t) =>
          t.decision.toLowerCase().includes(searchLower) ||
          t.reasoning.toLowerCase().includes(searchLower)
      )
    }

    if (query.minConfidence !== undefined) {
      results = results.filter((t) => t.confidence >= query.minConfidence!)
    }

    // Sort by timestamp (newest first)
    results.sort((a, b) => new Date(b.timestamp).getTime() - new Date(a.timestamp).getTime())

    const total = results.length
    const limit = query.limit || 50
    const hasMore = total > limit

    // Apply limit
    results = results.slice(0, limit)

    // Include precedents if requested
    if (query.includePrecedents) {
      results = results.map((trace) => ({
        ...trace,
        _precedents: this.getPrecedents(trace.id),
      })) as DecisionTrace[]
    }

    return { traces: results, total, hasMore }
  }

  /**
   * Get the precedent chain for a decision
   */
  getPrecedentChain(id: string): PrecedentChain | null {
    this.ensureLoaded()

    const root = this.traces.get(id)
    if (!root) return null

    const chain: DecisionTrace[] = []
    const visited = new Set<string>()
    let current = root

    // Walk back through precedents
    while (current.precedentIds && current.precedentIds.length > 0) {
      const precedentId = current.precedentIds[0] // Follow first precedent
      if (visited.has(precedentId)) break // Prevent cycles

      visited.add(precedentId)
      const precedent = this.traces.get(precedentId)
      if (!precedent) break

      chain.unshift(precedent) // Add to beginning (oldest first)
      current = precedent
    }

    return {
      root,
      chain,
      depth: chain.length,
    }
  }

  /**
   * Get statistics about traces
   */
  getStats(): TraceStats {
    this.ensureLoaded()

    const byType: Record<string, number> = {}
    const confidenceByType: Record<string, number[]> = {}
    const precedentCounts: Map<string, number> = new Map()

    // Initialize all types
    for (const type of DecisionTypeSchema.options) {
      byType[type] = 0
      confidenceByType[type] = []
    }

    // Count traces
    for (const trace of this.traces.values()) {
      byType[trace.type]++
      confidenceByType[trace.type].push(trace.confidence)

      // Count precedent references
      if (trace.precedentIds) {
        for (const pId of trace.precedentIds) {
          precedentCounts.set(pId, (precedentCounts.get(pId) || 0) + 1)
        }
      }
    }

    // Calculate averages
    const avgConfidenceByType: Record<string, number> = {}
    for (const type of DecisionTypeSchema.options) {
      const confidences = confidenceByType[type]
      avgConfidenceByType[type] =
        confidences.length > 0
          ? Math.round((confidences.reduce((a, b) => a + b, 0) / confidences.length) * 100) / 100
          : 0
    }

    // Get top precedents
    const topPrecedents = Array.from(precedentCounts.entries())
      .sort((a, b) => b[1] - a[1])
      .slice(0, 10)
      .map(([id, count]) => {
        const trace = this.traces.get(id)
        return {
          id,
          decision: trace?.decision || 'Unknown',
          referenceCount: count,
        }
      })

    // Count recent (last 24h)
    const dayAgo = Date.now() - 24 * 60 * 60 * 1000
    const recentCount = Array.from(this.traces.values()).filter(
      (t) => new Date(t.timestamp).getTime() > dayAgo
    ).length

    return {
      total: this.traces.size,
      byType: byType as Record<DecisionType, number>,
      avgConfidenceByType: avgConfidenceByType as Record<DecisionType, number>,
      topPrecedents,
      recentCount,
    }
  }

  /**
   * Find similar past decisions
   */
  findSimilar(type: DecisionType, context: string, limit = 5): DecisionTrace[] {
    this.ensureLoaded()

    const contextLower = context.toLowerCase()
    const words = new Set(contextLower.split(/\s+/).filter((w) => w.length > 3))

    // Score traces by type match and text similarity
    const scored = Array.from(this.traces.values())
      .filter((t) => t.type === type)
      .map((trace) => {
        const textLower = `${trace.decision} ${trace.reasoning}`.toLowerCase()
        const textWords = new Set(textLower.split(/\s+/).filter((w) => w.length > 3))

        // Jaccard similarity
        const intersection = new Set([...words].filter((w) => textWords.has(w)))
        const union = new Set([...words, ...textWords])
        const similarity = union.size > 0 ? intersection.size / union.size : 0

        return { trace, similarity }
      })
      .filter((s) => s.similarity > 0.1)
      .sort((a, b) => b.similarity - a.similarity)
      .slice(0, limit)

    return scored.map((s) => s.trace)
  }

  // ===========================================================================
  // Helper Methods
  // ===========================================================================

  private getPrecedents(id: string): DecisionTrace[] {
    const trace = this.traces.get(id)
    if (!trace?.precedentIds) return []

    return trace.precedentIds
      .map((pId) => this.traces.get(pId))
      .filter((t): t is DecisionTrace => t !== undefined)
  }

  private ensureLoaded(): void {
    if (this.loaded) return
    this.load()
    this.loaded = true
  }

  private load(): void {
    if (!existsSync(this.storePath)) return

    try {
      const content = readFileSync(this.storePath, 'utf-8')
      const lines = content.trim().split('\n').filter(Boolean)

      for (const line of lines) {
        try {
          const trace = DecisionTraceSchema.parse(JSON.parse(line))
          this.traces.set(trace.id, trace)
        } catch {
          // Skip invalid lines
        }
      }
    } catch {
      // File read error
    }
  }

  private appendTrace(trace: DecisionTrace): void {
    try {
      const dir = join(this.storePath, '..')
      if (!existsSync(dir)) {
        mkdirSync(dir, { recursive: true })
      }
      appendFileSync(this.storePath, JSON.stringify(trace) + '\n')
    } catch {
      // Write error - continue without persistence
    }
  }

  private emitTraceEvent(trace: DecisionTrace): void {
    try {
      const eventStore = getEventStore()
      eventStore.append('decision.traced', {
        traceId: trace.id,
        decisionType: trace.type,
        decision: trace.decision.slice(0, 200),
        confidence: trace.confidence,
        hasPrecedents: (trace.precedentIds?.length || 0) > 0,
      })
    } catch {
      // Event store may not be initialized
    }
  }

  private trimMemory(): void {
    if (this.traces.size <= this.maxMemoryTraces) return

    // Remove oldest traces from memory (keep on disk)
    const sorted = Array.from(this.traces.entries()).sort(
      (a, b) => new Date(a[1].timestamp).getTime() - new Date(b[1].timestamp).getTime()
    )

    const toRemove = sorted.slice(0, this.traces.size - this.maxMemoryTraces)
    for (const [id] of toRemove) {
      this.traces.delete(id)
    }
  }

  /**
   * Clear all traces (for testing)
   */
  clear(): void {
    this.traces.clear()
    this.loaded = false
  }
}

// =============================================================================
// Singleton
// =============================================================================

let globalTraceStore: TraceStore | null = null

export function getTraceStore(options?: TraceStoreOptions): TraceStore {
  if (!globalTraceStore) {
    globalTraceStore = new TraceStore(options)
  }
  return globalTraceStore
}

export function resetTraceStore(): void {
  globalTraceStore = null
}
