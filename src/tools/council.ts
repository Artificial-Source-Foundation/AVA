/**
 * Delta9 Council Tools
 *
 * Tools for consulting the council of oracles.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import type { CouncilMode } from '../types/config.js'
import type { OpenCodeClient } from '../lib/background-manager.js'
import { conveneCouncil, quickConsult, shouldConveneCouncil } from '../orchestration/index.js'
import { getConfidenceLabel } from '../lib/confidence-levels.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create council tools
 *
 * @param state - MissionState instance
 * @param cwd - Project root directory
 * @param client - OpenCode SDK client for real oracle invocation (without this, oracles run in simulation mode)
 */
export function createCouncilTools(
  state: MissionState,
  cwd: string,
  client?: OpenCodeClient
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
          client, // Pass client for real oracle invocation
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
          const confidences = opinions.map((o) => o.confidence)
          const maxConf = Math.max(...confidences)
          const minConf = Math.min(...confidences)
          if (maxConf - minConf > 0.3) {
            conflicts.push(
              `Confidence spread: ${(minConf * 100).toFixed(0)}%-${(maxConf * 100).toFixed(0)}%`
            )
          }

          // Check for opposing views in caveats
          const allCaveats = opinions.flatMap((o) => o.caveats || [])
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

  /**
   * Detailed council deliberation transparency
   */
  const council_deliberation = tool({
    description: `Get detailed transparency into council deliberation process.

**Purpose:** Understand how the council reached its decisions.

**Shows:**
- Individual oracle reasoning and vote
- Consensus building process
- Conflict detection and resolution
- Confidence distribution
- Alternative perspectives considered

**Use when:**
- Debugging unexpected council outcomes
- Auditing decision-making process
- Understanding oracle disagreements
- Reviewing consensus quality`,

    args: {
      verbose: s
        .boolean()
        .optional()
        .describe('Include full reasoning text (default: false)'),
    },

    async execute(args) {
      const mission = state.getMission()
      const councilSummary = mission?.councilSummary

      if (!councilSummary) {
        return JSON.stringify({
          success: false,
          error: 'No council deliberation in current mission',
          message: 'Use consult_council first to initiate deliberation',
        })
      }

      const opinions = councilSummary.opinions || []

      // Analyze confidence distribution
      const confidences = opinions.map((o) => o.confidence)
      const avgConfidence = confidences.length > 0
        ? confidences.reduce((a, b) => a + b, 0) / confidences.length
        : 0
      const stdDev = confidences.length > 1
        ? Math.sqrt(
            confidences.reduce((sum, c) => sum + Math.pow(c - avgConfidence, 2), 0) /
            (confidences.length - 1)
          )
        : 0

      // Categorize opinions by confidence level
      const highConfidence = opinions.filter((o) => o.confidence >= 0.8)
      const mediumConfidence = opinions.filter((o) => o.confidence >= 0.5 && o.confidence < 0.8)
      const lowConfidence = opinions.filter((o) => o.confidence < 0.5)

      // Extract all caveats and concerns
      const allCaveats: Array<{ oracle: string; caveat: string }> = []
      for (const opinion of opinions) {
        for (const caveat of opinion.caveats || []) {
          allCaveats.push({ oracle: opinion.oracle, caveat })
        }
      }

      // Extract all suggested tasks
      const allSuggestedTasks: Array<{ oracle: string; task: string }> = []
      for (const opinion of opinions) {
        for (const task of opinion.suggestedTasks || []) {
          allSuggestedTasks.push({ oracle: opinion.oracle, task })
        }
      }

      // Detect disagreements and conflicts
      const conflicts: Array<{
        type: string
        description: string
        oracles: string[]
      }> = []

      // Check for confidence spread
      if (confidences.length > 1 && stdDev > 0.15) {
        const lowOracles = lowConfidence.map((o) => o.oracle)
        const highOracles = highConfidence.map((o) => o.oracle)
        if (lowOracles.length > 0 && highOracles.length > 0) {
          conflicts.push({
            type: 'confidence_divergence',
            description: `Significant confidence spread (stdDev: ${(stdDev * 100).toFixed(1)}%)`,
            oracles: [...lowOracles, ...highOracles],
          })
        }
      }

      // Check for recommendation length variance (may indicate different approaches)
      const recLengths = opinions.map((o) => o.recommendation.length)
      const avgLength = recLengths.reduce((a, b) => a + b, 0) / recLengths.length
      const shortRecs = opinions.filter((o) => o.recommendation.length < avgLength * 0.5)
      if (shortRecs.length > 0 && opinions.length > 2) {
        conflicts.push({
          type: 'approach_variance',
          description: 'Some oracles provided significantly shorter responses',
          oracles: shortRecs.map((o) => o.oracle),
        })
      }

      // Build detailed opinion breakdown
      const opinionDetails = opinions.map((o) => ({
        oracle: o.oracle,
        confidence: o.confidence,
        confidenceLabel: getConfidenceLabel(o.confidence),
        recommendationLength: o.recommendation.length,
        recommendation: args.verbose
          ? o.recommendation
          : o.recommendation.substring(0, 300) + (o.recommendation.length > 300 ? '...' : ''),
        caveatsCount: o.caveats?.length || 0,
        caveats: o.caveats,
        suggestedTasksCount: o.suggestedTasks?.length || 0,
        suggestedTasks: o.suggestedTasks,
      }))

      // Build consensus analysis
      const consensusAnalysis = {
        points: councilSummary.consensus,
        strength: avgConfidence >= 0.7 && stdDev < 0.15 ? 'strong' :
                  avgConfidence >= 0.5 ? 'moderate' : 'weak',
        disagreementsResolved: councilSummary.disagreementsResolved || [],
        unanimity: stdDev < 0.1 && avgConfidence > 0.6,
      }

      return JSON.stringify({
        success: true,
        deliberation: {
          mode: councilSummary.mode,
          oracleCount: opinions.length,
          timestamp: new Date().toISOString(),
        },
        confidenceAnalysis: {
          average: avgConfidence,
          standardDeviation: stdDev,
          distribution: {
            high: highConfidence.length,
            medium: mediumConfidence.length,
            low: lowConfidence.length,
          },
          label: getConfidenceLabel(avgConfidence),
        },
        consensusAnalysis,
        opinions: opinionDetails,
        caveatsRaised: {
          total: allCaveats.length,
          items: allCaveats,
        },
        suggestedTasks: {
          total: allSuggestedTasks.length,
          items: allSuggestedTasks,
        },
        conflicts: conflicts.length > 0 ? conflicts : undefined,
        transparency: {
          allOraclesResponded: opinions.length > 0,
          consensusReached: consensusAnalysis.strength !== 'weak',
          conflictsDetected: conflicts.length,
          caveatsAddressed: (councilSummary.disagreementsResolved || []).length,
        },
      }, null, 2)
    },
  })

  return {
    consult_council,
    quick_consult,
    should_consult_council,
    council_status,
    council_deliberation,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type CouncilTools = ReturnType<typeof createCouncilTools>
