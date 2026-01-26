/**
 * Delta9 Intent Classification
 *
 * Pre-planning phase that classifies user requests.
 * Inspired by oh-my-opencode's Metis agent.
 *
 * Philosophy: "Understand the intent before planning the work."
 */

// =============================================================================
// Types
// =============================================================================

/**
 * Intent types - each has different planning focus
 */
export type IntentType =
  | 'refactoring' // Safety focus - regression prevention
  | 'build' // Discovery focus - explore patterns first
  | 'fix' // Precision focus - minimal targeted changes
  | 'architecture' // Strategic focus - council consultation recommended
  | 'research' // Investigation focus - gather info before acting

/**
 * Intent classification result
 */
export interface IntentClassification {
  /** Primary intent type */
  intent: IntentType
  /** Confidence score (0-1) */
  confidence: number
  /** Keywords that matched */
  matchedKeywords: string[]
  /** Recommended planning approach */
  planningFocus: string
  /** Suggested tools/agents */
  suggestedTools: string[]
  /** Risk considerations */
  riskFactors: string[]
}

// =============================================================================
// Keyword Patterns
// =============================================================================

interface IntentPattern {
  keywords: string[]
  planningFocus: string
  suggestedTools: string[]
  riskFactors: string[]
}

const INTENT_PATTERNS: Record<IntentType, IntentPattern> = {
  refactoring: {
    keywords: [
      'refactor',
      'restructure',
      'clean up',
      'reorganize',
      'rename',
      'extract',
      'move',
      'split',
      'merge',
      'simplify',
      'consolidate',
      'decouple',
      'modularize',
    ],
    planningFocus: 'SAFETY: Prevent regressions. Test before and after.',
    suggestedTools: ['validator', 'run_tests', 'check_types'],
    riskFactors: [
      'Changes may break dependent code',
      'Ensure test coverage before refactoring',
      'Use small, incremental changes',
    ],
  },

  build: {
    keywords: [
      'create',
      'add',
      'implement',
      'build',
      'new',
      'develop',
      'introduce',
      'setup',
      'initialize',
      'generate',
      'make',
      'write',
      'feature',
    ],
    planningFocus: 'DISCOVERY: Explore existing patterns first. Match codebase style.',
    suggestedTools: ['explorer', 'scout', 'intel'],
    riskFactors: [
      'Study existing patterns before implementation',
      'Check for similar existing code',
      'Consider future maintainability',
    ],
  },

  fix: {
    keywords: [
      'fix',
      'bug',
      'broken',
      'error',
      'issue',
      'problem',
      'fail',
      'crash',
      'wrong',
      'incorrect',
      'not working',
      'debug',
      'resolve',
      'repair',
    ],
    planningFocus: 'PRECISION: Minimal targeted changes. Verify root cause first.',
    suggestedTools: ['validator', 'run_tests', 'check_types', 'intel'],
    riskFactors: [
      'Identify root cause before fixing',
      'Avoid masking symptoms',
      'Add regression test for the fix',
    ],
  },

  architecture: {
    keywords: [
      'architect',
      'design',
      'structure',
      'how should',
      'pattern',
      'approach',
      'strategy',
      'tradeoff',
      'decision',
      'evaluate',
      'compare',
      'recommend',
      'best way',
      'scalable',
    ],
    planningFocus: 'STRATEGIC: Council consultation recommended. Consider tradeoffs.',
    suggestedTools: ['consult_council', 'explorer', 'intel'],
    riskFactors: [
      'Document decision rationale',
      'Consider long-term implications',
      'Consult council for complex decisions',
    ],
  },

  research: {
    keywords: [
      'investigate',
      'explore',
      'research',
      'find out',
      'understand',
      'analyze',
      'discover',
      'learn',
      'check',
      'look into',
      'examine',
      'study',
      'what is',
      'how does',
    ],
    planningFocus: 'INVESTIGATION: Gather information before acting. Define exit criteria.',
    suggestedTools: ['explorer', 'scout', 'intel'],
    riskFactors: ['Define clear exit criteria', 'Time-box exploration', 'Document findings'],
  },
}

// =============================================================================
// Classifier
// =============================================================================

/**
 * Classify user request intent.
 *
 * @param request - User's request/description
 * @returns Intent classification result
 */
export function classifyIntent(request: string): IntentClassification {
  const lower = request.toLowerCase()
  const scores: Record<IntentType, { score: number; matches: string[] }> = {
    refactoring: { score: 0, matches: [] },
    build: { score: 0, matches: [] },
    fix: { score: 0, matches: [] },
    architecture: { score: 0, matches: [] },
    research: { score: 0, matches: [] },
  }

  // Score each intent type based on keyword matches
  for (const [intent, pattern] of Object.entries(INTENT_PATTERNS)) {
    for (const keyword of pattern.keywords) {
      if (lower.includes(keyword.toLowerCase())) {
        scores[intent as IntentType].score++
        scores[intent as IntentType].matches.push(keyword)
      }
    }
  }

  // Find the highest scoring intent
  let bestIntent: IntentType = 'build' // Default
  let bestScore = 0

  for (const [intent, data] of Object.entries(scores)) {
    if (data.score > bestScore) {
      bestScore = data.score
      bestIntent = intent as IntentType
    }
  }

  // Calculate confidence (normalized by number of keywords in winning pattern)
  const pattern = INTENT_PATTERNS[bestIntent]
  const maxPossibleMatches = Math.min(pattern.keywords.length, 5) // Cap at 5
  const confidence = Math.min(bestScore / maxPossibleMatches, 1)

  return {
    intent: bestIntent,
    confidence,
    matchedKeywords: scores[bestIntent].matches,
    planningFocus: pattern.planningFocus,
    suggestedTools: pattern.suggestedTools,
    riskFactors: pattern.riskFactors,
  }
}

/**
 * Format classification for Commander prompt injection.
 */
export function formatClassificationForPrompt(classification: IntentClassification): string {
  const lines: string[] = []

  lines.push('## INTENT CLASSIFICATION')
  lines.push('')
  lines.push(`**Type:** ${classification.intent.toUpperCase()}`)
  lines.push(`**Confidence:** ${Math.round(classification.confidence * 100)}%`)
  lines.push(`**Focus:** ${classification.planningFocus}`)
  lines.push('')

  if (classification.matchedKeywords.length > 0) {
    lines.push(`**Matched Keywords:** ${classification.matchedKeywords.join(', ')}`)
    lines.push('')
  }

  lines.push('**Suggested Tools:**')
  for (const tool of classification.suggestedTools) {
    lines.push(`- ${tool}`)
  }
  lines.push('')

  lines.push('**Risk Factors:**')
  for (const risk of classification.riskFactors) {
    lines.push(`- ${risk}`)
  }

  return lines.join('\n')
}

/**
 * Get short intent label for display.
 */
export function getIntentLabel(intent: IntentType): string {
  const labels: Record<IntentType, string> = {
    refactoring: 'Refactoring',
    build: 'Build/Create',
    fix: 'Bug Fix',
    architecture: 'Architecture',
    research: 'Research',
  }
  return labels[intent]
}
