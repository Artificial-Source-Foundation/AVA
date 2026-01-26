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

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// Decision type options for schema
const DECISION_TYPES = DecisionTypeSchema.options

// =============================================================================
// Tool Factory
// =============================================================================

export function createTraceTools(): Record<string, ToolDefinition> {
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

  return {
    trace_decision,
    query_traces,
    get_trace,
    find_similar_decisions,
    trace_stats,
  }
}

export const TRACE_TOOL_NAMES = [
  'trace_decision',
  'query_traces',
  'get_trace',
  'find_similar_decisions',
  'trace_stats',
] as const
