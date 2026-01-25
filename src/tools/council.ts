/**
 * Delta9 Council Tools
 *
 * Tools for consulting the council of oracles.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import type { CouncilMode } from '../types/config.js'
import { conveneCouncil, quickConsult, shouldConveneCouncil } from '../orchestration/index.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * Get human-readable confidence label
 */
function getConfidenceLabel(confidence: number): string {
  if (confidence >= 0.9) return 'Very High'
  if (confidence >= 0.7) return 'High'
  if (confidence >= 0.5) return 'Moderate'
  if (confidence >= 0.3) return 'Low'
  return 'Very Low'
}

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
   * Get council configuration, status, and last deliberation results
   */
  const council_status = tool({
    description: `Get the current council configuration and deliberation status.
Shows:
- Council configuration (enabled oracles, modes)
- Last deliberation results (if any)
- Individual oracle opinions with confidence
- Consensus points and conflicts`,

    args: {
      includeFullOpinions: s
        .boolean()
        .optional()
        .describe('Include full recommendation text (default: false, shows summary only)'),
    },

    async execute(args, _ctx) {
      const { loadConfig, getEnabledOracles } = await import('../lib/config.js')
      const { oracleProfiles } = await import('../agents/council/index.js')
      const config = loadConfig(cwd)
      const enabledOracles = getEnabledOracles(cwd)
      const mission = state.getMission()
      const councilSummary = mission?.councilSummary

      // Build oracle details with Delta Team profiles
      const oracleDetails = enabledOracles.map((o) => {
        const profile = oracleProfiles[o.name as keyof typeof oracleProfiles]
        return {
          codename: o.name,
          role: profile?.role || 'Oracle',
          model: o.model,
          specialty: o.specialty,
          temperature: o.temperature ?? profile?.temperature,
          traits: profile?.traits?.slice(0, 2) || [],
        }
      })

      // Build last deliberation info
      let lastDeliberation = null
      if (councilSummary) {
        const opinions = councilSummary.opinions || []

        // Detect conflicts (significantly different confidence or opposing caveats)
        const conflicts: string[] = []
        if (opinions.length > 1) {
          const confidences = opinions.map(o => o.confidence)
          const maxConf = Math.max(...confidences)
          const minConf = Math.min(...confidences)
          if (maxConf - minConf > 0.3) {
            conflicts.push(`Confidence spread: ${(minConf * 100).toFixed(0)}%-${(maxConf * 100).toFixed(0)}%`)
          }

          // Check for opposing views in caveats
          const allCaveats = opinions.flatMap(o => o.caveats || [])
          if (allCaveats.length > 3) {
            conflicts.push(`${allCaveats.length} caveats raised across oracles`)
          }
        }

        lastDeliberation = {
          mode: councilSummary.mode,
          oracleCount: opinions.length,
          confidenceAvg: councilSummary.confidenceAvg,
          consensus: councilSummary.consensus,
          disagreementsResolved: councilSummary.disagreementsResolved,
          conflicts: conflicts.length > 0 ? conflicts : undefined,
          opinions: opinions.map((o) => ({
            oracle: o.oracle,
            confidence: o.confidence,
            confidenceLabel: getConfidenceLabel(o.confidence),
            recommendation: args.includeFullOpinions
              ? o.recommendation
              : o.recommendation.substring(0, 200) + (o.recommendation.length > 200 ? '...' : ''),
            caveats: o.caveats,
            suggestedTasks: o.suggestedTasks,
          })),
        }
      }

      return JSON.stringify({
        success: true,
        config: {
          enabled: config.council.enabled,
          defaultMode: config.council.defaultMode,
          autoDetectComplexity: config.council.autoDetectComplexity,
          parallel: config.council.parallel,
          requireConsensus: config.council.requireConsensus,
          minResponses: config.council.minResponses,
          timeoutSeconds: config.council.timeoutSeconds,
        },
        deltaTeam: {
          total: config.council.members.length,
          enabled: enabledOracles.length,
          members: oracleDetails,
        },
        lastDeliberation,
        message: lastDeliberation
          ? `Council active. Last deliberation: ${lastDeliberation.oracleCount} oracles, ${(lastDeliberation.confidenceAvg * 100).toFixed(0)}% avg confidence`
          : 'Council configured. No deliberation in current mission.',
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
