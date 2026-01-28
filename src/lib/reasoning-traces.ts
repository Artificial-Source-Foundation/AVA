/**
 * Delta9 Agent Reasoning Traces (C-1)
 *
 * Captures agent decision-making steps for:
 * - Debugging complex agent behavior
 * - Understanding why decisions were made
 * - Auditing agent actions
 * - Improving agent performance over time
 */

import { getNamedLogger } from './logger.js'

const log = getNamedLogger('reasoning-traces')

// =============================================================================
// Types
// =============================================================================

/** Types of reasoning steps */
export type ReasoningStepType =
  | 'observation' // Something the agent noticed
  | 'analysis' // Agent's analysis of a situation
  | 'hypothesis' // A theory or assumption
  | 'decision' // A decision made
  | 'action' // An action taken
  | 'verification' // Verification of results
  | 'reflection' // Reflection on outcome
  | 'escalation' // Decision to escalate
  | 'delegation' // Decision to delegate
  | 'error' // Error encountered

/** A single reasoning step */
export interface ReasoningStep {
  /** Unique step ID */
  id: string
  /** Step type */
  type: ReasoningStepType
  /** Timestamp */
  timestamp: number
  /** Agent that made this step */
  agent: string
  /** Brief title/action */
  title: string
  /** Detailed reasoning/explanation */
  reasoning: string
  /** Input/context that led to this step */
  input?: string
  /** Output/result of this step */
  output?: string
  /** Confidence level (0-1) */
  confidence?: number
  /** Alternative options considered */
  alternatives?: Array<{
    option: string
    reason: string
    rejected: boolean
  }>
  /** Tools used in this step */
  toolsUsed?: string[]
  /** References to other steps */
  relatedSteps?: string[]
  /** Duration of this step in ms */
  durationMs?: number
  /** Metadata */
  metadata?: Record<string, unknown>
}

/** A complete reasoning trace for a task/session */
export interface ReasoningTrace {
  /** Trace ID */
  id: string
  /** Session ID */
  sessionId: string
  /** Task ID (if applicable) */
  taskId?: string
  /** Mission ID (if applicable) */
  missionId?: string
  /** Primary agent */
  primaryAgent: string
  /** Description of what's being traced */
  description: string
  /** When trace started */
  startedAt: number
  /** When trace completed */
  completedAt?: number
  /** All reasoning steps */
  steps: ReasoningStep[]
  /** Final outcome */
  outcome?: {
    success: boolean
    summary: string
    confidence: number
  }
  /** Tags for categorization */
  tags?: string[]
}

/** Configuration for the reasoning tracer */
export interface ReasoningTracerConfig {
  /** Maximum steps per trace */
  maxStepsPerTrace?: number
  /** Maximum traces to keep in memory */
  maxTraces?: number
  /** Auto-export traces older than this (ms, 0 = disabled) */
  autoExportAgeMs?: number
  /** Callback when trace completes */
  onTraceComplete?: (trace: ReasoningTrace) => void | Promise<void>
  /** Callback for each step (for real-time monitoring) */
  onStep?: (step: ReasoningStep, trace: ReasoningTrace) => void | Promise<void>
}

// =============================================================================
// Reasoning Tracer
// =============================================================================

export class ReasoningTracer {
  private traces: Map<string, ReasoningTrace> = new Map()
  private activeTraces: Map<string, string> = new Map() // sessionId -> traceId
  private stepCounter = 0
  private traceCounter = 0
  private config: Required<ReasoningTracerConfig>

  constructor(config: ReasoningTracerConfig = {}) {
    this.config = {
      maxStepsPerTrace: config.maxStepsPerTrace ?? 500,
      maxTraces: config.maxTraces ?? 100,
      autoExportAgeMs: config.autoExportAgeMs ?? 0,
      onTraceComplete: config.onTraceComplete ?? (() => {}),
      onStep: config.onStep ?? (() => {}),
    }
  }

  // ===========================================================================
  // Trace Lifecycle
  // ===========================================================================

  /**
   * Start a new reasoning trace
   */
  startTrace(params: {
    sessionId: string
    taskId?: string
    missionId?: string
    primaryAgent: string
    description: string
    tags?: string[]
  }): ReasoningTrace {
    const traceId = `trace_${++this.traceCounter}_${Date.now()}`

    const trace: ReasoningTrace = {
      id: traceId,
      sessionId: params.sessionId,
      taskId: params.taskId,
      missionId: params.missionId,
      primaryAgent: params.primaryAgent,
      description: params.description,
      startedAt: Date.now(),
      steps: [],
      tags: params.tags,
    }

    this.traces.set(traceId, trace)
    this.activeTraces.set(params.sessionId, traceId)

    // Cleanup old traces if needed
    this.cleanupOldTraces()

    log.debug(`Started trace: ${traceId}`, {
      agent: params.primaryAgent,
      description: params.description,
    })

    return trace
  }

  /**
   * End a reasoning trace
   */
  async endTrace(
    traceId: string,
    outcome: { success: boolean; summary: string; confidence: number }
  ): Promise<ReasoningTrace | null> {
    const trace = this.traces.get(traceId)
    if (!trace) {
      log.warn(`Trace not found: ${traceId}`)
      return null
    }

    trace.completedAt = Date.now()
    trace.outcome = outcome

    // Remove from active traces
    for (const [sessionId, activeTraceId] of this.activeTraces) {
      if (activeTraceId === traceId) {
        this.activeTraces.delete(sessionId)
        break
      }
    }

    log.info(`Completed trace: ${traceId}`, {
      success: outcome.success,
      steps: trace.steps.length,
      durationMs: trace.completedAt - trace.startedAt,
    })

    // Call completion callback
    await this.config.onTraceComplete(trace)

    return trace
  }

  /**
   * Get active trace for a session
   */
  getActiveTrace(sessionId: string): ReasoningTrace | null {
    const traceId = this.activeTraces.get(sessionId)
    if (!traceId) return null
    return this.traces.get(traceId) ?? null
  }

  /**
   * Get or create trace for a session
   */
  getOrCreateTrace(params: {
    sessionId: string
    taskId?: string
    missionId?: string
    primaryAgent: string
    description: string
  }): ReasoningTrace {
    const existing = this.getActiveTrace(params.sessionId)
    if (existing) return existing
    return this.startTrace(params)
  }

  // ===========================================================================
  // Step Recording
  // ===========================================================================

  /**
   * Add a reasoning step to a trace
   */
  async addStep(
    traceId: string,
    step: Omit<ReasoningStep, 'id' | 'timestamp'>
  ): Promise<ReasoningStep | null> {
    const trace = this.traces.get(traceId)
    if (!trace) {
      log.warn(`Cannot add step - trace not found: ${traceId}`)
      return null
    }

    if (trace.steps.length >= this.config.maxStepsPerTrace) {
      log.warn(`Trace ${traceId} has reached max steps (${this.config.maxStepsPerTrace})`)
      return null
    }

    const fullStep: ReasoningStep = {
      ...step,
      id: `step_${++this.stepCounter}_${Date.now()}`,
      timestamp: Date.now(),
    }

    trace.steps.push(fullStep)

    log.debug(`Added step to trace ${traceId}`, {
      stepType: step.type,
      agent: step.agent,
      title: step.title,
    })

    // Call step callback
    await this.config.onStep(fullStep, trace)

    return fullStep
  }

  /**
   * Add step by session ID (convenience method)
   */
  async addStepBySession(
    sessionId: string,
    step: Omit<ReasoningStep, 'id' | 'timestamp'>
  ): Promise<ReasoningStep | null> {
    const traceId = this.activeTraces.get(sessionId)
    if (!traceId) {
      log.warn(`No active trace for session: ${sessionId}`)
      return null
    }
    return this.addStep(traceId, step)
  }

  // ===========================================================================
  // Convenience Methods for Common Steps
  // ===========================================================================

  /**
   * Record an observation
   */
  async observe(
    sessionId: string,
    agent: string,
    title: string,
    details: string,
    metadata?: Record<string, unknown>
  ): Promise<ReasoningStep | null> {
    return this.addStepBySession(sessionId, {
      type: 'observation',
      agent,
      title,
      reasoning: details,
      metadata,
    })
  }

  /**
   * Record an analysis
   */
  async analyze(
    sessionId: string,
    agent: string,
    title: string,
    analysis: string,
    alternatives?: Array<{ option: string; reason: string; rejected: boolean }>
  ): Promise<ReasoningStep | null> {
    return this.addStepBySession(sessionId, {
      type: 'analysis',
      agent,
      title,
      reasoning: analysis,
      alternatives,
    })
  }

  /**
   * Record a decision
   */
  async decide(
    sessionId: string,
    agent: string,
    title: string,
    reasoning: string,
    confidence: number,
    alternatives?: Array<{ option: string; reason: string; rejected: boolean }>
  ): Promise<ReasoningStep | null> {
    return this.addStepBySession(sessionId, {
      type: 'decision',
      agent,
      title,
      reasoning,
      confidence,
      alternatives,
    })
  }

  /**
   * Record an action
   */
  async act(
    sessionId: string,
    agent: string,
    title: string,
    reasoning: string,
    toolsUsed?: string[],
    output?: string
  ): Promise<ReasoningStep | null> {
    return this.addStepBySession(sessionId, {
      type: 'action',
      agent,
      title,
      reasoning,
      toolsUsed,
      output,
    })
  }

  /**
   * Record an error
   */
  async recordError(
    sessionId: string,
    agent: string,
    title: string,
    error: string,
    metadata?: Record<string, unknown>
  ): Promise<ReasoningStep | null> {
    return this.addStepBySession(sessionId, {
      type: 'error',
      agent,
      title,
      reasoning: error,
      confidence: 0,
      metadata,
    })
  }

  // ===========================================================================
  // Query & Export
  // ===========================================================================

  /**
   * Get a trace by ID
   */
  getTrace(traceId: string): ReasoningTrace | undefined {
    return this.traces.get(traceId)
  }

  /**
   * Get all traces (optionally filtered)
   */
  getTraces(filter?: {
    sessionId?: string
    taskId?: string
    missionId?: string
    agent?: string
    completed?: boolean
    tags?: string[]
  }): ReasoningTrace[] {
    let traces = Array.from(this.traces.values())

    if (filter) {
      if (filter.sessionId) {
        traces = traces.filter((t) => t.sessionId === filter.sessionId)
      }
      if (filter.taskId) {
        traces = traces.filter((t) => t.taskId === filter.taskId)
      }
      if (filter.missionId) {
        traces = traces.filter((t) => t.missionId === filter.missionId)
      }
      if (filter.agent) {
        traces = traces.filter((t) => t.primaryAgent === filter.agent)
      }
      if (filter.completed !== undefined) {
        traces = traces.filter((t) => (t.completedAt !== undefined) === filter.completed)
      }
      if (filter.tags && filter.tags.length > 0) {
        traces = traces.filter((t) => filter.tags!.some((tag) => t.tags?.includes(tag)))
      }
    }

    return traces.sort((a, b) => b.startedAt - a.startedAt)
  }

  /**
   * Export trace to JSON
   */
  exportTrace(traceId: string): string | null {
    const trace = this.traces.get(traceId)
    if (!trace) return null
    return JSON.stringify(trace, null, 2)
  }

  /**
   * Export trace to Markdown
   */
  exportTraceMarkdown(traceId: string): string | null {
    const trace = this.traces.get(traceId)
    if (!trace) return null
    return formatTraceMarkdown(trace)
  }

  // ===========================================================================
  // Maintenance
  // ===========================================================================

  /**
   * Cleanup old traces
   */
  private cleanupOldTraces(): void {
    if (this.traces.size <= this.config.maxTraces) return

    // Sort by startedAt and remove oldest
    const sorted = Array.from(this.traces.entries()).sort(
      ([, a], [, b]) => a.startedAt - b.startedAt
    )

    const toRemove = sorted.slice(0, this.traces.size - this.config.maxTraces)
    for (const [id] of toRemove) {
      // Don't remove active traces
      let isActive = false
      for (const activeId of this.activeTraces.values()) {
        if (activeId === id) {
          isActive = true
          break
        }
      }
      if (!isActive) {
        this.traces.delete(id)
      }
    }
  }

  /**
   * Clear all traces
   */
  clear(): void {
    this.traces.clear()
    this.activeTraces.clear()
    log.debug('Cleared all traces')
  }

  /**
   * Get statistics
   */
  getStats(): {
    totalTraces: number
    activeTraces: number
    completedTraces: number
    totalSteps: number
    avgStepsPerTrace: number
  } {
    const traces = Array.from(this.traces.values())
    const completed = traces.filter((t) => t.completedAt !== undefined)
    const totalSteps = traces.reduce((sum, t) => sum + t.steps.length, 0)

    return {
      totalTraces: traces.length,
      activeTraces: this.activeTraces.size,
      completedTraces: completed.length,
      totalSteps,
      avgStepsPerTrace: traces.length > 0 ? totalSteps / traces.length : 0,
    }
  }
}

// =============================================================================
// Factory Functions
// =============================================================================

/** Singleton instance */
let defaultTracer: ReasoningTracer | null = null

/**
 * Get or create the default reasoning tracer
 */
export function getReasoningTracer(config?: ReasoningTracerConfig): ReasoningTracer {
  if (!defaultTracer) {
    defaultTracer = new ReasoningTracer(config)
  }
  return defaultTracer
}

/**
 * Reset the default reasoning tracer (for testing)
 */
export function resetReasoningTracer(): void {
  defaultTracer = null
}

/**
 * Create a new reasoning tracer
 */
export function createReasoningTracer(config?: ReasoningTracerConfig): ReasoningTracer {
  return new ReasoningTracer(config)
}

// =============================================================================
// Formatting Utilities
// =============================================================================

/**
 * Format a reasoning trace as Markdown
 */
export function formatTraceMarkdown(trace: ReasoningTrace): string {
  const lines: string[] = []

  lines.push(`# Reasoning Trace: ${trace.description}`)
  lines.push('')
  lines.push(`**Trace ID:** ${trace.id}`)
  lines.push(`**Agent:** ${trace.primaryAgent}`)
  lines.push(`**Started:** ${new Date(trace.startedAt).toISOString()}`)
  if (trace.completedAt) {
    lines.push(`**Completed:** ${new Date(trace.completedAt).toISOString()}`)
    lines.push(`**Duration:** ${trace.completedAt - trace.startedAt}ms`)
  }
  if (trace.tags && trace.tags.length > 0) {
    lines.push(`**Tags:** ${trace.tags.join(', ')}`)
  }
  lines.push('')

  if (trace.outcome) {
    lines.push('## Outcome')
    lines.push('')
    lines.push(`**Status:** ${trace.outcome.success ? '✅ Success' : '❌ Failed'}`)
    lines.push(`**Confidence:** ${(trace.outcome.confidence * 100).toFixed(0)}%`)
    lines.push(`**Summary:** ${trace.outcome.summary}`)
    lines.push('')
  }

  lines.push('## Reasoning Steps')
  lines.push('')

  for (let i = 0; i < trace.steps.length; i++) {
    const step = trace.steps[i]
    const icon = getStepIcon(step.type)

    lines.push(`### ${i + 1}. ${icon} ${step.title}`)
    lines.push('')
    lines.push(`**Type:** ${step.type} | **Agent:** ${step.agent}`)
    if (step.confidence !== undefined) {
      lines.push(`**Confidence:** ${(step.confidence * 100).toFixed(0)}%`)
    }
    lines.push('')
    lines.push(step.reasoning)
    lines.push('')

    if (step.alternatives && step.alternatives.length > 0) {
      lines.push('**Alternatives Considered:**')
      for (const alt of step.alternatives) {
        const status = alt.rejected ? '❌' : '✅'
        lines.push(`- ${status} ${alt.option}: ${alt.reason}`)
      }
      lines.push('')
    }

    if (step.toolsUsed && step.toolsUsed.length > 0) {
      lines.push(`**Tools Used:** ${step.toolsUsed.join(', ')}`)
      lines.push('')
    }

    if (step.output) {
      lines.push('**Output:**')
      lines.push('```')
      lines.push(step.output.slice(0, 500))
      if (step.output.length > 500) lines.push('...')
      lines.push('```')
      lines.push('')
    }
  }

  return lines.join('\n')
}

/**
 * Get icon for step type
 */
function getStepIcon(type: ReasoningStepType): string {
  const icons: Record<ReasoningStepType, string> = {
    observation: '👁️',
    analysis: '🔍',
    hypothesis: '💡',
    decision: '⚖️',
    action: '⚡',
    verification: '✓',
    reflection: '🤔',
    escalation: '⬆️',
    delegation: '👥',
    error: '❌',
  }
  return icons[type] || '•'
}

/**
 * Format a single step for logging
 */
export function formatStepForLog(step: ReasoningStep): string {
  const icon = getStepIcon(step.type)
  let msg = `${icon} [${step.agent}] ${step.title}`
  if (step.confidence !== undefined) {
    msg += ` (${(step.confidence * 100).toFixed(0)}% confidence)`
  }
  return msg
}

/**
 * Get summary statistics for a trace
 */
export function getTraceSummary(trace: ReasoningTrace): {
  totalSteps: number
  stepsByType: Record<string, number>
  agents: string[]
  totalDurationMs: number
  avgConfidence: number
} {
  const stepsByType: Record<string, number> = {}
  const agents = new Set<string>()
  let totalConfidence = 0
  let confidenceCount = 0

  for (const step of trace.steps) {
    stepsByType[step.type] = (stepsByType[step.type] || 0) + 1
    agents.add(step.agent)
    if (step.confidence !== undefined) {
      totalConfidence += step.confidence
      confidenceCount++
    }
  }

  return {
    totalSteps: trace.steps.length,
    stepsByType,
    agents: Array.from(agents),
    totalDurationMs: (trace.completedAt || Date.now()) - trace.startedAt,
    avgConfidence: confidenceCount > 0 ? totalConfidence / confidenceCount : 0,
  }
}
