/**
 * Delta9 Council Tools
 *
 * Tools for consulting the council of oracles.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import type { CouncilMode } from '../types/config.js'
import { conveneCouncil, quickConsult, shouldConveneCouncil } from '../council/index.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create council tools
 */
export function createCouncilTools(
  state: MissionState,
  cwd: string
): Record<string, ToolDefinition> {
  /**
   * Consult the council of oracles for strategic decisions
   */
  const consult_council = tool({
    description: `Consult the Council of oracles for strategic decisions.
Use for complex decisions requiring multiple perspectives:
- Architecture choices
- Risk assessment
- Trade-off analysis
- Implementation strategy

Modes:
- quick: Single oracle, fast response
- standard: All enabled oracles, balanced
- xhigh: All oracles with thorough analysis`,

    args: {
      question: s.string().describe('Question for the council'),
      mode: s
        .enum(['quick', 'standard', 'xhigh'])
        .optional()
        .describe('Council mode (default: standard)'),
      context: s.string().optional().describe('Additional context for the oracles'),
    },

    async execute(args, _ctx) {
      const mode = (args.mode || 'standard') as CouncilMode

      try {
        const result = await conveneCouncil(state, cwd, {
          question: args.question,
          mode,
          context: args.context,
        })

        return JSON.stringify({
          success: true,
          mode,
          summary: {
            consensus: result.summary.consensus,
            confidenceAvg: result.summary.confidenceAvg,
            oracleCount: result.results.length,
            disagreementsResolved: result.summary.disagreementsResolved,
          },
          opinions: result.summary.opinions?.map((o) => ({
            oracle: o.oracle,
            confidence: o.confidence,
            recommendation: o.recommendation.substring(0, 500),
            hasCaveats: !!(o.caveats && o.caveats.length > 0),
            hasSuggestedTasks: !!(o.suggestedTasks && o.suggestedTasks.length > 0),
          })),
          stats: {
            totalTokens: result.totalTokens,
            totalDurationMs: result.totalDurationMs,
          },
          message: `Council consulted successfully. ${result.results.length} oracle(s) responded.`,
        })
      } catch (error) {
        return JSON.stringify({
          success: false,
          mode,
          error: error instanceof Error ? error.message : String(error),
          message: 'Council consultation failed',
        })
      }
    },
  })

  /**
   * Quick consultation with single oracle
   */
  const quick_consult = tool({
    description: 'Quick consultation with a single oracle for simple questions.',

    args: {
      question: s.string().describe('Question for quick consultation'),
    },

    async execute(args, _ctx) {
      const opinion = await quickConsult(state, cwd, args.question)

      if (!opinion) {
        return JSON.stringify({
          success: false,
          message: 'No oracles available for consultation',
        })
      }

      return JSON.stringify({
        success: true,
        oracle: opinion.oracle,
        recommendation: opinion.recommendation,
        confidence: opinion.confidence,
        caveats: opinion.caveats,
        suggestedTasks: opinion.suggestedTasks,
      })
    },
  })

  /**
   * Check if council should be convened for a topic
   */
  const should_consult_council = tool({
    description: 'Check if the council should be convened based on topic keywords.',

    args: {
      topic: s.string().describe('Topic or keywords to analyze'),
    },

    async execute(args, _ctx) {
      // Extract keywords from topic
      const keywords = args.topic
        .toLowerCase()
        .split(/\s+/)
        .filter((w) => w.length > 3)

      const { shouldConvene, suggestedMode } = shouldConveneCouncil(cwd, keywords)

      return JSON.stringify({
        success: true,
        shouldConvene,
        suggestedMode,
        analyzedKeywords: keywords.slice(0, 10),
        message: shouldConvene
          ? `Council consultation recommended with mode: ${suggestedMode}`
          : 'Council consultation not required for this topic',
      })
    },
  })

  /**
   * Get council configuration summary
   */
  const council_status = tool({
    description: 'Get the current council configuration and status.',

    args: {},

    async execute(_args, _ctx) {
      const { loadConfig, getEnabledOracles } = await import('../lib/config.js')
      const config = loadConfig(cwd)
      const enabledOracles = getEnabledOracles(cwd)

      return JSON.stringify({
        success: true,
        enabled: config.council.enabled,
        defaultMode: config.council.defaultMode,
        autoDetectComplexity: config.council.autoDetectComplexity,
        parallel: config.council.parallel,
        requireConsensus: config.council.requireConsensus,
        minResponses: config.council.minResponses,
        timeoutSeconds: config.council.timeoutSeconds,
        oracles: {
          total: config.council.members.length,
          enabled: enabledOracles.length,
          list: enabledOracles.map((o) => ({
            name: o.name,
            model: o.model,
            specialty: o.specialty,
          })),
        },
      })
    },
  })

  return {
    consult_council,
    quick_consult,
    should_consult_council,
    council_status,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type CouncilTools = ReturnType<typeof createCouncilTools>
