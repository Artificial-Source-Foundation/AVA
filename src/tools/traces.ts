/**
 * Delta9 Decision Trace Tools
 *
 * Tools for recording and querying decision traces.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import {
  getTraceStore,
  DecisionTypeSchema,
  type CreateTraceInput,
  type TraceQuery,
} from '../traces/index.js'
import { getReasoningTracer } from '../lib/reasoning-traces.js'
import { readHistory } from '../mission/history.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// Decision type options for schema
const DECISION_TYPES = DecisionTypeSchema.options

// =============================================================================
// Tool Factory
// =============================================================================

export function createTraceTools(cwd?: string): Record<string, ToolDefinition> {
  /**
   * Record a decision with reasoning
   */
  const trace_decision = tool({
    description: `Record WHY a decision was made. Use this to create an audit trail of reasoning.

Decision types:
- decomposition_strategy: Why this task breakdown approach?
- agent_assignment: Why this agent for this task?
- council_consensus: How did oracles reach agreement?
- validation_override: Why override a validation result?
- conflict_resolution: How was a conflict resolved?
- model_selection: Why this model for this task?
- retry_strategy: Why retry vs fail?
- priority_change: Why change task priority?
- task_skip: Why skip this task?
- budget_decision: Why approve/deny budget?

ALWAYS record decisions for:
- Task decomposition strategy choices
- Agent assignments
- Council consensus results
- Validation overrides
- Any conflict resolution`,
    args: {
      type: s.enum(DECISION_TYPES).describe('Type of decision being recorded'),
      decision: s.string().describe('What was decided (the outcome)'),
      reasoning: s.string().describe('Why this decision was made (the reasoning)'),
      alternatives: s.array(s.string()).optional().describe('What alternatives were considered'),
      confidence: s.number().describe('Confidence level in the decision (0-1)'),
      precedent_ids: s
        .array(s.string())
        .optional()
        .describe('IDs of prior decisions that informed this one'),
      task_id: s.string().optional().describe('Related task ID'),
      mission_id: s.string().optional().describe('Related mission ID'),
      agent_id: s.string().optional().describe('Agent making the decision'),
      files: s.array(s.string()).optional().describe('Files related to this decision'),
      tags: s.array(s.string()).optional().describe('Tags for categorization'),
    },

    async execute(args, _ctx) {
      const store = getTraceStore()

      const createInput: CreateTraceInput = {
        type: args.type,
        decision: args.decision,
        reasoning: args.reasoning,
        alternatives: args.alternatives,
        confidence: args.confidence,
        precedentIds: args.precedent_ids,
        context: {
          taskId: args.task_id,
          missionId: args.mission_id,
          agentId: args.agent_id,
          files: args.files,
          tags: args.tags,
        },
      }

      const trace = store.record(createInput)

      return JSON.stringify({
        success: true,
        trace_id: trace.id,
        type: trace.type,
        decision: trace.decision,
        confidence: trace.confidence,
        timestamp: trace.timestamp,
        message: `Decision traced: ${trace.type} - ${trace.decision.slice(0, 100)}...`,
      })
    },
  })

  /**
   * Query traces with filters
   */
  const query_traces = tool({
    description: `Search past decision traces to find precedents and understand decision history.

Use this to:
- Find similar past decisions before making a new one
- Understand why previous decisions were made
- Build on precedent when making related decisions
- Audit decision history for a task/mission`,
    args: {
      type: s.enum(DECISION_TYPES).optional().describe('Filter by decision type'),
      task_id: s.string().optional().describe('Filter by task ID'),
      mission_id: s.string().optional().describe('Filter by mission ID'),
      agent_id: s.string().optional().describe('Filter by agent ID'),
      search: s.string().optional().describe('Search in decision/reasoning text'),
      since: s.string().optional().describe('Filter traces since timestamp (ISO format)'),
      min_confidence: s.number().optional().describe('Minimum confidence threshold (0-1)'),
      limit: s.number().optional().describe('Maximum results to return (default: 20)'),
      include_precedents: s.boolean().optional().describe('Include precedent chain for each trace'),
    },

    async execute(args, _ctx) {
      const store = getTraceStore()

      const query: TraceQuery = {
        type: args.type,
        taskId: args.task_id,
        missionId: args.mission_id,
        agentId: args.agent_id,
        search: args.search,
        since: args.since,
        minConfidence: args.min_confidence,
        limit: args.limit || 20,
        includePrecedents: args.include_precedents || false,
      }

      const result = store.query(query)

      return JSON.stringify({
        success: true,
        total: result.total,
        has_more: result.hasMore,
        traces: result.traces.map((t) => ({
          id: t.id,
          type: t.type,
          decision: t.decision,
          reasoning: t.reasoning.slice(0, 200) + (t.reasoning.length > 200 ? '...' : ''),
          confidence: t.confidence,
          timestamp: t.timestamp,
          precedent_count: t.precedentIds?.length || 0,
        })),
      })
    },
  })

  /**
   * Get a specific trace by ID
   */
  const get_trace = tool({
    description: 'Get a specific decision trace by ID, including its full precedent chain.',
    args: {
      trace_id: s.string().describe('The trace ID to retrieve'),
      include_chain: s
        .boolean()
        .optional()
        .describe('Include the full precedent chain (default: true)'),
    },

    async execute(args, _ctx) {
      const store = getTraceStore()

      const trace = store.get(args.trace_id)
      if (!trace) {
        return JSON.stringify({
          success: false,
          error: `Trace not found: ${args.trace_id}`,
        })
      }

      const result: Record<string, unknown> = {
        success: true,
        trace: {
          id: trace.id,
          type: trace.type,
          decision: trace.decision,
          reasoning: trace.reasoning,
          alternatives: trace.alternatives,
          confidence: trace.confidence,
          context: trace.context,
          timestamp: trace.timestamp,
          decided_by: trace.decidedBy,
        },
      }

      if (args.include_chain !== false) {
        const chain = store.getPrecedentChain(args.trace_id)
        if (chain) {
          result.precedent_chain = {
            depth: chain.depth,
            chain: chain.chain.map((t) => ({
              id: t.id,
              type: t.type,
              decision: t.decision,
              confidence: t.confidence,
              timestamp: t.timestamp,
            })),
          }
        }
      }

      return JSON.stringify(result)
    },
  })

  /**
   * Find similar past decisions
   */
  const find_similar_decisions = tool({
    description: `Find similar past decisions to inform a new decision.

Use BEFORE making important decisions to:
- Learn from past successes/failures
- Build on established precedents
- Maintain consistency in decision-making`,
    args: {
      type: s.enum(DECISION_TYPES).describe('Type of decision to search for'),
      context: s.string().describe('Description of the current situation/context'),
      limit: s.number().optional().describe('Maximum similar decisions to return (default: 5)'),
    },

    async execute(args, _ctx) {
      const store = getTraceStore()

      const similar = store.findSimilar(args.type, args.context, args.limit || 5)

      return JSON.stringify({
        success: true,
        count: similar.length,
        similar: similar.map((t) => ({
          id: t.id,
          type: t.type,
          decision: t.decision,
          reasoning: t.reasoning.slice(0, 200) + (t.reasoning.length > 200 ? '...' : ''),
          confidence: t.confidence,
          timestamp: t.timestamp,
        })),
        message:
          similar.length > 0
            ? `Found ${similar.length} similar past decisions to consider`
            : 'No similar past decisions found - this may be a novel situation',
      })
    },
  })

  /**
   * Get trace statistics
   */
  const trace_stats = tool({
    description: 'Get statistics about decision traces - types, confidence levels, top precedents.',
    args: {},

    async execute(_args, _ctx) {
      const store = getTraceStore()
      const stats = store.getStats()

      return JSON.stringify({
        success: true,
        total_traces: stats.total,
        recent_24h: stats.recentCount,
        by_type: stats.byType,
        avg_confidence_by_type: stats.avgConfidenceByType,
        top_precedents: stats.topPrecedents.slice(0, 5),
      })
    },
  })

  /**
   * View and export agent reasoning traces
   */
  const delta9_reasoning = tool({
    description: `View agent reasoning traces showing step-by-step thought process.

**Purpose:** Debug agent decisions and understand reasoning chains.

**Operations:**
- list: Show all reasoning sessions
- view: View specific trace steps
- export: Export trace as markdown/JSON
- stats: Get reasoning statistics

**Example:**
delta9_reasoning({ op: "list" })
delta9_reasoning({ op: "view", sessionId: "sess-123" })
delta9_reasoning({ op: "export", sessionId: "sess-123", format: "markdown" })
delta9_reasoning({ op: "stats" })

**Use when:**
- Debugging why an agent made a specific decision
- Understanding reasoning flow
- Extracting lessons from tasks`,

    args: {
      op: s.enum(['list', 'view', 'export', 'stats']).describe('Operation to perform'),
      sessionId: s.string().optional().describe('Session ID for view/export operations'),
      format: s.enum(['json', 'markdown']).optional().describe('Export format (default: json)'),
      limit: s.number().optional().describe('Limit number of results'),
    },

    async execute(args) {
      const tracer = getReasoningTracer()
      const { op, sessionId, format = 'json', limit = 20 } = args

      switch (op) {
        case 'list': {
          const traces = tracer.getTraces().slice(0, limit)
          const summaries = traces.map((trace) => ({
            traceId: trace.id,
            sessionId: trace.sessionId,
            agent: trace.primaryAgent,
            description: trace.description,
            stepCount: trace.steps.length,
            startedAt: new Date(trace.startedAt).toISOString(),
            completed: trace.completedAt !== undefined,
          }))

          return JSON.stringify(
            {
              success: true,
              traces: summaries,
              total: tracer.getTraces().length,
            },
            null,
            2
          )
        }

        case 'view': {
          if (!sessionId) {
            return JSON.stringify({ success: false, error: 'sessionId (or traceId) required' })
          }

          // Try to find by traceId first, then by sessionId
          let trace = tracer.getTrace(sessionId)
          if (!trace) {
            const traces = tracer.getTraces({ sessionId })
            trace = traces[0]
          }

          if (!trace) {
            return JSON.stringify({ success: false, error: `Trace not found: ${sessionId}` })
          }

          return JSON.stringify(
            {
              success: true,
              trace: {
                id: trace.id,
                sessionId: trace.sessionId,
                agent: trace.primaryAgent,
                description: trace.description,
                startedAt: new Date(trace.startedAt).toISOString(),
                completedAt: trace.completedAt
                  ? new Date(trace.completedAt).toISOString()
                  : undefined,
                outcome: trace.outcome,
                steps: trace.steps.map((step, i) => ({
                  index: i + 1,
                  type: step.type,
                  title: step.title,
                  reasoning: step.reasoning,
                  confidence: step.confidence,
                  alternatives: step.alternatives,
                })),
              },
            },
            null,
            2
          )
        }

        case 'export': {
          if (!sessionId) {
            return JSON.stringify({ success: false, error: 'sessionId (or traceId) required' })
          }

          let trace = tracer.getTrace(sessionId)
          if (!trace) {
            const traces = tracer.getTraces({ sessionId })
            trace = traces[0]
          }

          if (!trace) {
            return JSON.stringify({ success: false, error: `Trace not found: ${sessionId}` })
          }

          if (format === 'markdown') {
            const markdown = tracer.exportTraceMarkdown(trace.id)
            return JSON.stringify({ success: true, format: 'markdown', content: markdown }, null, 2)
          }

          return JSON.stringify({ success: true, format: 'json', content: trace }, null, 2)
        }

        case 'stats': {
          const stats = tracer.getStats()
          return JSON.stringify({ success: true, statistics: stats }, null, 2)
        }

        default:
          return JSON.stringify({ success: false, error: `Unknown operation: ${op}` })
      }
    },
  })

  /**
   * Decision audit log for compliance and debugging
   */
  const delta9_audit = tool({
    description: `View decision audit log for compliance and debugging.

**Purpose:** Track and review all significant decisions made by agents.

**Features:**
- Chronological decision log from history + traces
- Filter by agent, time period, or decision type
- Compliance checking (Commander delegation, Validator gates)
- Decision pattern analysis

**Example:**
delta9_audit()                        # Recent decisions
delta9_audit({ agent: "commander" })  # Commander decisions only
delta9_audit({ period: "1h" })        # Last hour
delta9_audit({ type: "delegation" })  # Delegation decisions

**Use when:**
- Auditing agent decision compliance
- Reviewing delegation patterns
- Debugging unexpected behaviors`,

    args: {
      agent: s.string().optional().describe('Filter by agent name'),
      period: s
        .enum(['1h', '24h', '7d', 'all'])
        .optional()
        .describe('Time period filter (default: 24h)'),
      type: s
        .enum(['all', 'delegation', 'execution', 'validation', 'council', 'recovery'])
        .optional()
        .describe('Decision type filter'),
      limit: s.number().optional().describe('Maximum entries (default: 50)'),
    },

    async execute(args) {
      const { agent, type = 'all', limit = 50 } = args
      const period = args.period ?? '24h'

      // Calculate time boundary
      let sinceTimestamp: number | undefined
      if (period !== 'all') {
        const periodMs: Record<string, number> = {
          '1h': 60 * 60 * 1000,
          '24h': 24 * 60 * 60 * 1000,
          '7d': 7 * 24 * 60 * 60 * 1000,
        }
        sinceTimestamp = Date.now() - periodMs[period]
      }

      // Collect audit entries
      const auditEntries: AuditEntry[] = []

      // 1. From mission history
      if (cwd) {
        try {
          const history = readHistory(cwd)
          for (const entry of history) {
            const entryTime = new Date(entry.timestamp).getTime()
            if (sinceTimestamp && entryTime < sinceTimestamp) continue

            const auditEntry = mapHistoryToAudit(entry)
            if (!auditEntry) continue
            if (agent && auditEntry.agent !== agent) continue
            if (type !== 'all' && !auditEntry.action.includes(type)) continue

            auditEntries.push(auditEntry)
          }
        } catch {
          // History may not exist
        }
      }

      // 2. From reasoning traces
      const tracer = getReasoningTracer()
      const traces = tracer.getTraces()
      for (const trace of traces) {
        if (sinceTimestamp && trace.startedAt < sinceTimestamp) continue

        for (const step of trace.steps) {
          if (step.type !== 'decision') continue

          const auditEntry: AuditEntry = {
            timestamp: new Date(step.timestamp).toISOString(),
            sessionId: trace.sessionId,
            agent: trace.primaryAgent,
            action: step.title,
            decision: step.reasoning,
            alternatives: step.alternatives?.map((a) => `${a.option}: ${a.reason}`),
            confidence: step.confidence,
          }

          if (agent && auditEntry.agent !== agent) continue
          if (type !== 'all' && !categorizeDecision(step.title).includes(type)) continue

          auditEntries.push(auditEntry)
        }
      }

      // Sort by timestamp (newest first)
      auditEntries.sort((a, b) => new Date(b.timestamp).getTime() - new Date(a.timestamp).getTime())

      // Apply limit
      const limited = auditEntries.slice(0, limit)

      // Build summary
      const agentCounts: Record<string, number> = {}
      const typeCounts: Record<string, number> = {}
      for (const entry of auditEntries) {
        agentCounts[entry.agent] = (agentCounts[entry.agent] || 0) + 1
        const decisionType = categorizeDecision(entry.action)
        typeCounts[decisionType] = (typeCounts[decisionType] || 0) + 1
      }

      return JSON.stringify(
        {
          success: true,
          entries: limited,
          summary: {
            total: auditEntries.length,
            returned: limited.length,
            byAgent: agentCounts,
            byType: typeCounts,
          },
          filters: { agent, type, period },
        },
        null,
        2
      )
    },
  })

  return {
    trace_decision,
    query_traces,
    get_trace,
    find_similar_decisions,
    trace_stats,
    delta9_reasoning,
    delta9_audit,
  }
}

export const TRACE_TOOL_NAMES = [
  'trace_decision',
  'query_traces',
  'get_trace',
  'find_similar_decisions',
  'trace_stats',
  'delta9_reasoning',
  'delta9_audit',
] as const

// =============================================================================
// Audit Types and Helpers
// =============================================================================

interface AuditEntry {
  timestamp: string
  sessionId?: string
  agent: string
  action: string
  decision: string
  alternatives?: string[]
  confidence?: number
}

interface HistoryEntry {
  timestamp: string
  type: string
  missionId?: string
  data?: Record<string, unknown>
}

/**
 * Map history entry to audit entry
 */
function mapHistoryToAudit(entry: HistoryEntry): AuditEntry | null {
  const data = entry.data || {}

  switch (entry.type) {
    case 'council_convened':
      return {
        timestamp: entry.timestamp,
        agent: 'commander',
        action: 'council_delegation',
        decision: `Convened council in ${data.mode} mode with ${data.oracleCount} oracles`,
      }

    case 'council_completed':
      return {
        timestamp: entry.timestamp,
        agent: 'council',
        action: 'council_decision',
        decision: `Council reached consensus with ${data.confidenceAvg} confidence`,
        confidence: data.confidenceAvg as number,
      }

    case 'task_delegated':
      return {
        timestamp: entry.timestamp,
        agent: (data.delegatedBy as string) || 'commander',
        action: 'task_delegation',
        decision: `Delegated task ${data.taskId} to ${data.agent}`,
      }

    case 'task_completed':
      return {
        timestamp: entry.timestamp,
        agent: (data.agent as string) || 'operator',
        action: 'task_execution',
        decision: `Completed task ${data.taskId}`,
      }

    case 'task_failed':
      return {
        timestamp: entry.timestamp,
        agent: (data.agent as string) || 'operator',
        action: 'task_execution',
        decision: `Failed task ${data.taskId}: ${data.error}`,
      }

    case 'validation_passed':
      return {
        timestamp: entry.timestamp,
        agent: 'validator',
        action: 'validation_decision',
        decision: `Validated task ${data.taskId}`,
      }

    case 'validation_failed':
      return {
        timestamp: entry.timestamp,
        agent: 'validator',
        action: 'validation_decision',
        decision: `Rejected task ${data.taskId}: ${data.reason}`,
      }

    case 'recovery_attempted':
      return {
        timestamp: entry.timestamp,
        agent: (data.agent as string) || 'system',
        action: 'recovery_decision',
        decision: `Attempted recovery strategy: ${data.strategy}`,
      }

    default:
      return null
  }
}

/**
 * Categorize decision action into type
 */
function categorizeDecision(action: string): string {
  if (action.includes('delegation') || action.includes('delegate')) return 'delegation'
  if (action.includes('execution') || action.includes('execute')) return 'execution'
  if (action.includes('validation') || action.includes('validate')) return 'validation'
  if (action.includes('council')) return 'council'
  if (action.includes('recovery')) return 'recovery'
  return 'other'
}
