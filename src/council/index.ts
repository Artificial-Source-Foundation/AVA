/**
 * Delta9 Council Module
 *
 * Orchestrates multi-oracle deliberation for complex decisions.
 * Aggregates opinions and builds consensus.
 */

import type { MissionState } from '../mission/state.js'
import type { CouncilMode } from '../types/config.js'
import type { CouncilSummary, OracleOpinion } from '../types/mission.js'
import { loadConfig, getEnabledOracles } from '../lib/config.js'
import { getCouncilModels } from '../lib/models.js'
import { appendHistory } from '../mission/history.js'
import {
  invokeOraclesParallel,
  invokeOraclesSequential,
  type OraclePromptContext,
  type OracleInvocationResult,
} from './oracle.js'

// =============================================================================
// Re-exports
// =============================================================================

export {
  buildOracleSystemPrompt,
  buildOracleUserPrompt,
  parseOracleResponse,
  invokeOracle,
  invokeOraclesParallel,
  invokeOraclesSequential,
  type OraclePromptContext,
  type OracleInvocationResult,
} from './oracle.js'

// =============================================================================
// Types
// =============================================================================

export interface ConveneCouncilInput {
  /** Question for the council */
  question: string
  /** Council mode */
  mode: CouncilMode
  /** Additional context */
  context?: string
}

export interface CouncilResult {
  /** Council summary */
  summary: CouncilSummary
  /** Individual oracle results */
  results: OracleInvocationResult[]
  /** Total tokens used */
  totalTokens: number
  /** Total duration in ms */
  totalDurationMs: number
}

// =============================================================================
// Council Orchestration
// =============================================================================

/**
 * Convene the council for deliberation
 */
export async function conveneCouncil(
  state: MissionState,
  cwd: string,
  input: ConveneCouncilInput
): Promise<CouncilResult> {
  const config = loadConfig(cwd)
  const mission = state.getMission()

  // Get oracles for this mode
  const oracles = getCouncilModels(cwd, input.mode)

  if (oracles.length === 0) {
    return {
      summary: {
        mode: input.mode,
        consensus: ['No oracles available for consultation'],
        confidenceAvg: 0,
      },
      results: [],
      totalTokens: 0,
      totalDurationMs: 0,
    }
  }

  // Log council convening
  if (mission) {
    appendHistory(cwd, {
      type: 'council_convened',
      timestamp: new Date().toISOString(),
      missionId: mission.id,
      data: {
        mode: input.mode,
        oracleCount: oracles.length,
        oracles: oracles.map((o) => o.name),
      },
    })
  }

  // Build context for oracles
  const context: OraclePromptContext = {
    question: input.question,
    missionDescription: mission?.description || 'No active mission',
    objectivesSummary: buildObjectivesSummary(state),
    additionalContext: input.context,
  }

  // Invoke oracles
  const startTime = Date.now()
  let results: OracleInvocationResult[]

  if (config.council.parallel) {
    results = await invokeOraclesParallel(oracles, context)
  } else {
    results = await invokeOraclesSequential(oracles, context)
  }

  const totalDurationMs = Date.now() - startTime
  const totalTokens = results.reduce((sum, r) => sum + r.tokensUsed, 0)

  // Aggregate opinions into summary
  const summary = aggregateOpinions(results, input.mode, config.council.requireConsensus)

  // Log council completion
  if (mission) {
    appendHistory(cwd, {
      type: 'council_completed',
      timestamp: new Date().toISOString(),
      missionId: mission.id,
      data: {
        mode: input.mode,
        confidenceAvg: summary.confidenceAvg,
        consensusPoints: summary.consensus.length,
        totalTokens,
        totalDurationMs,
      },
    })
  }

  // Store summary in mission state
  if (mission) {
    state.updateMission({ councilSummary: summary })
  }

  return {
    summary,
    results,
    totalTokens,
    totalDurationMs,
  }
}

/**
 * Build objectives summary for oracle context
 */
function buildObjectivesSummary(state: MissionState): string {
  const mission = state.getMission()
  if (!mission || mission.objectives.length === 0) {
    return 'No objectives defined yet.'
  }

  const lines: string[] = []
  for (const objective of mission.objectives) {
    const taskCount = objective.tasks.length
    const completedCount = objective.tasks.filter((t) => t.status === 'completed').length
    lines.push(`- ${objective.description} (${completedCount}/${taskCount} tasks complete)`)
  }

  return lines.join('\n')
}

/**
 * Aggregate oracle opinions into a council summary
 */
function aggregateOpinions(
  results: OracleInvocationResult[],
  mode: CouncilMode,
  requireConsensus: boolean
): CouncilSummary {
  const opinions = results.map((r) => r.opinion)

  // Calculate average confidence
  const confidenceAvg =
    opinions.length > 0
      ? opinions.reduce((sum, o) => sum + o.confidence, 0) / opinions.length
      : 0

  // Extract consensus points
  const consensus = extractConsensus(opinions)

  // Find disagreements that were resolved
  const disagreementsResolved = requireConsensus
    ? findResolvedDisagreements(opinions)
    : undefined

  return {
    mode,
    consensus,
    disagreementsResolved,
    confidenceAvg,
    opinions: opinions.length > 0 ? opinions : undefined,
  }
}

/**
 * Extract consensus points from opinions
 */
function extractConsensus(opinions: OracleOpinion[]): string[] {
  if (opinions.length === 0) {
    return []
  }

  if (opinions.length === 1) {
    // Single oracle - use their main points
    const points = extractKeyPoints(opinions[0].recommendation)
    return points.slice(0, 3)
  }

  // Multiple oracles - find common themes
  const allPoints = opinions.flatMap((o) => extractKeyPoints(o.recommendation))

  // Simple frequency-based consensus
  const pointCounts = new Map<string, number>()
  for (const point of allPoints) {
    const normalized = normalizePoint(point)
    pointCounts.set(normalized, (pointCounts.get(normalized) || 0) + 1)
  }

  // Points mentioned by majority
  const threshold = Math.ceil(opinions.length / 2)
  const consensusPoints = Array.from(pointCounts.entries())
    .filter(([, count]) => count >= threshold)
    .sort((a, b) => b[1] - a[1])
    .slice(0, 5)
    .map(([point]) => point)

  return consensusPoints.length > 0
    ? consensusPoints
    : ['Oracles provided diverse perspectives without clear consensus']
}

/**
 * Extract key points from a recommendation
 */
function extractKeyPoints(recommendation: string): string[] {
  // Split by common delimiters and filter
  const lines = recommendation.split(/[.\n]/).filter((line) => {
    const trimmed = line.trim()
    return trimmed.length > 20 && trimmed.length < 200
  })

  return lines.slice(0, 5).map((l) => l.trim())
}

/**
 * Normalize a point for comparison
 */
function normalizePoint(point: string): string {
  return point.toLowerCase().trim().substring(0, 100)
}

/**
 * Find disagreements that were resolved
 */
function findResolvedDisagreements(opinions: OracleOpinion[]): string[] {
  // Identify caveats that were addressed by other oracles
  const allCaveats = opinions.flatMap((o) => o.caveats || [])
  const allRecommendations = opinions.map((o) => o.recommendation.toLowerCase())

  const resolved: string[] = []
  for (const caveat of allCaveats) {
    const caveatLower = caveat.toLowerCase()
    // Check if any recommendation addresses this caveat
    const isAddressed = allRecommendations.some(
      (rec) =>
        rec.includes('address') ||
        rec.includes('mitigate') ||
        rec.includes('handle') ||
        rec.includes(caveatLower.split(' ').slice(0, 3).join(' '))
    )
    if (isAddressed) {
      resolved.push(caveat)
    }
  }

  return resolved.slice(0, 3)
}

// =============================================================================
// Quick Consultation
// =============================================================================

/**
 * Quick council consultation for simple decisions
 *
 * Uses the first enabled oracle only.
 */
export async function quickConsult(
  state: MissionState,
  cwd: string,
  question: string
): Promise<OracleOpinion | null> {
  const oracles = getEnabledOracles(cwd)
  if (oracles.length === 0) {
    return null
  }

  const result = await conveneCouncil(state, cwd, {
    question,
    mode: 'quick',
  })

  return result.results[0]?.opinion ?? null
}

/**
 * Check if council should be convened based on task complexity
 */
export function shouldConveneCouncil(
  cwd: string,
  keywords: string[]
): { shouldConvene: boolean; suggestedMode: CouncilMode } {
  const config = loadConfig(cwd)

  if (!config.council.enabled) {
    return { shouldConvene: false, suggestedMode: 'none' }
  }

  // Check for keywords indicating complexity
  const xhighKeywords = config.seamless.keywords.councilXhigh
  const noneKeywords = config.seamless.keywords.councilNone

  const hasXhighKeyword = keywords.some((k) =>
    xhighKeywords.some((xk) => k.toLowerCase().includes(xk.toLowerCase()))
  )

  const hasNoneKeyword = keywords.some((k) =>
    noneKeywords.some((nk) => k.toLowerCase().includes(nk.toLowerCase()))
  )

  if (hasNoneKeyword) {
    return { shouldConvene: false, suggestedMode: 'none' }
  }

  if (hasXhighKeyword) {
    return { shouldConvene: true, suggestedMode: 'xhigh' }
  }

  // Default to config setting
  return {
    shouldConvene: config.council.enabled,
    suggestedMode: config.council.defaultMode,
  }
}
