/**
 * Delta9 Complexity Detection
 *
 * Analyzes tasks/requests to determine appropriate council mode.
 * Uses heuristics based on:
 * - Keywords and phrases
 * - Scope indicators
 * - File/component count estimates
 * - Risk indicators
 */

import type { CouncilMode } from '../types/config.js'
import type { Complexity } from '../types/mission.js'

// =============================================================================
// Types
// =============================================================================

export interface ComplexityAnalysis {
  /** Detected complexity level */
  complexity: Complexity
  /** Suggested council mode */
  suggestedMode: CouncilMode
  /** Confidence in this assessment (0-1) */
  confidence: number
  /** Reasons for this assessment */
  reasons: string[]
  /** Detected indicators */
  indicators: {
    keywords: string[]
    scope: 'single-file' | 'multi-file' | 'multi-component' | 'system-wide'
    risk: 'low' | 'medium' | 'high'
    estimatedFiles: number
  }
}

// =============================================================================
// Keyword Patterns
// =============================================================================

/** Keywords indicating critical/high complexity */
const CRITICAL_KEYWORDS = [
  'refactor',
  'rewrite',
  'migrate',
  'authentication',
  'authorization',
  'security',
  'database schema',
  'architecture',
  'core',
  'breaking change',
  'api change',
  'production',
  'critical',
  'urgent',
  'carefully',
]

/** Keywords indicating high complexity */
const HIGH_KEYWORDS = [
  'implement',
  'build',
  'create system',
  'integrate',
  'new feature',
  'complex',
  'multiple',
  'across',
  'entire',
  'comprehensive',
  'full',
  'complete',
  'redesign',
  'optimize',
  'performance',
]

/** Keywords indicating medium complexity */
const MEDIUM_KEYWORDS = [
  'add',
  'update',
  'modify',
  'change',
  'improve',
  'enhance',
  'extend',
  'component',
  'page',
  'endpoint',
  'feature',
  'function',
]

/** Keywords indicating low complexity */
const LOW_KEYWORDS = [
  'fix',
  'typo',
  'rename',
  'simple',
  'quick',
  'small',
  'minor',
  'tweak',
  'adjust',
  'single',
  'one',
  'just',
  'only',
  'style',
  'format',
  'lint',
  'comment',
]

/** Scope indicators */
const SCOPE_PATTERNS = {
  systemWide: [
    'all files',
    'entire codebase',
    'everywhere',
    'across the app',
    'global',
    'system-wide',
    'project-wide',
  ],
  multiComponent: [
    'multiple components',
    'several files',
    'many files',
    'frontend and backend',
    'client and server',
    'ui and api',
  ],
  multiFile: ['few files', 'couple of', 'and tests', 'with tests', 'component and'],
  singleFile: ['this file', 'single file', 'one file', 'in file', 'the file'],
}

/** Risk indicators */
const RISK_PATTERNS = {
  high: [
    'production',
    'database',
    'auth',
    'payment',
    'security',
    'sensitive',
    'critical path',
    'user data',
    'credentials',
    'encryption',
  ],
  medium: ['api', 'endpoint', 'state', 'cache', 'session', 'config', 'environment'],
  low: ['ui', 'style', 'formatting', 'comment', 'documentation', 'test'],
}

// =============================================================================
// Analysis Functions
// =============================================================================

/**
 * Analyze task complexity from description
 */
export function analyzeComplexity(description: string): ComplexityAnalysis {
  const text = description.toLowerCase()

  // Find matching keywords
  const foundKeywords: string[] = []
  let criticalScore = 0
  let highScore = 0
  let mediumScore = 0
  let lowScore = 0

  // Check critical keywords (weight: 3)
  for (const keyword of CRITICAL_KEYWORDS) {
    if (text.includes(keyword)) {
      criticalScore += 3
      foundKeywords.push(keyword)
    }
  }

  // Check high keywords (weight: 2)
  for (const keyword of HIGH_KEYWORDS) {
    if (text.includes(keyword)) {
      highScore += 2
      foundKeywords.push(keyword)
    }
  }

  // Check medium keywords (weight: 1)
  for (const keyword of MEDIUM_KEYWORDS) {
    if (text.includes(keyword)) {
      mediumScore += 1
      foundKeywords.push(keyword)
    }
  }

  // Check low keywords (weight: -1)
  for (const keyword of LOW_KEYWORDS) {
    if (text.includes(keyword)) {
      lowScore += 1
      foundKeywords.push(keyword)
    }
  }

  // Detect scope
  let scope: ComplexityAnalysis['indicators']['scope'] = 'single-file'
  let estimatedFiles = 1

  for (const pattern of SCOPE_PATTERNS.systemWide) {
    if (text.includes(pattern)) {
      scope = 'system-wide'
      estimatedFiles = 20
      break
    }
  }
  if (scope === 'single-file') {
    for (const pattern of SCOPE_PATTERNS.multiComponent) {
      if (text.includes(pattern)) {
        scope = 'multi-component'
        estimatedFiles = 8
        break
      }
    }
  }
  if (scope === 'single-file') {
    for (const pattern of SCOPE_PATTERNS.multiFile) {
      if (text.includes(pattern)) {
        scope = 'multi-file'
        estimatedFiles = 3
        break
      }
    }
  }

  // Detect risk level
  let risk: ComplexityAnalysis['indicators']['risk'] = 'low'
  for (const pattern of RISK_PATTERNS.high) {
    if (text.includes(pattern)) {
      risk = 'high'
      break
    }
  }
  if (risk === 'low') {
    for (const pattern of RISK_PATTERNS.medium) {
      if (text.includes(pattern)) {
        risk = 'medium'
        break
      }
    }
  }

  // Calculate total score
  const totalScore = criticalScore + highScore + mediumScore - lowScore

  // Add scope and risk modifiers
  const scopeModifier =
    scope === 'system-wide' ? 5 : scope === 'multi-component' ? 3 : scope === 'multi-file' ? 1 : 0

  const riskModifier = risk === 'high' ? 4 : risk === 'medium' ? 2 : 0

  const finalScore = totalScore + scopeModifier + riskModifier

  // Determine complexity and mode
  let complexity: Complexity
  let suggestedMode: CouncilMode
  const reasons: string[] = []

  if (finalScore >= 10 || risk === 'high') {
    complexity = 'critical'
    suggestedMode = 'xhigh'
    reasons.push('High-risk or critical task detected')
    if (criticalScore > 0) reasons.push('Contains critical keywords')
    if (risk === 'high') reasons.push('Involves high-risk areas')
    if (scope === 'system-wide') reasons.push('System-wide scope')
  } else if (finalScore >= 5 || scope === 'multi-component') {
    complexity = 'high'
    suggestedMode = 'standard'
    reasons.push('Complex task requiring multiple perspectives')
    if (highScore > 0) reasons.push('Contains complexity indicators')
    if (scope === 'multi-component') reasons.push('Multi-component scope')
  } else if (finalScore >= 2 || scope === 'multi-file') {
    complexity = 'medium'
    suggestedMode = 'quick'
    reasons.push('Moderate task benefiting from quick review')
    if (mediumScore > 0) reasons.push('Standard development task')
  } else {
    complexity = 'low'
    suggestedMode = 'none'
    reasons.push('Simple task, no council needed')
    if (lowScore > 0) reasons.push('Contains simplicity indicators')
  }

  // Calculate confidence based on keyword matches
  const totalMatches = foundKeywords.length
  const confidence = Math.min(0.95, 0.5 + totalMatches * 0.1)

  return {
    complexity,
    suggestedMode,
    confidence,
    reasons,
    indicators: {
      keywords: [...new Set(foundKeywords)].slice(0, 10),
      scope,
      risk,
      estimatedFiles,
    },
  }
}

/**
 * Get council mode from complexity
 */
export function complexityToCouncilMode(complexity: Complexity): CouncilMode {
  switch (complexity) {
    case 'critical':
      return 'xhigh'
    case 'high':
      return 'standard'
    case 'medium':
      return 'quick'
    case 'low':
      return 'none'
  }
}

/**
 * Check if a task should trigger council consultation
 */
export function shouldTriggerCouncil(
  description: string,
  minComplexity: Complexity = 'medium'
): boolean {
  const analysis = analyzeComplexity(description)

  const complexityOrder: Record<Complexity, number> = {
    low: 0,
    medium: 1,
    high: 2,
    critical: 3,
  }

  return complexityOrder[analysis.complexity] >= complexityOrder[minComplexity]
}

/**
 * Get human-readable complexity description
 */
export function describeComplexity(analysis: ComplexityAnalysis): string {
  const lines: string[] = []

  lines.push(`Complexity: ${analysis.complexity.toUpperCase()}`)
  lines.push(`Suggested Mode: ${analysis.suggestedMode.toUpperCase()}`)
  lines.push(`Confidence: ${(analysis.confidence * 100).toFixed(0)}%`)
  lines.push('')
  lines.push('Reasons:')
  for (const reason of analysis.reasons) {
    lines.push(`  - ${reason}`)
  }
  lines.push('')
  lines.push(`Scope: ${analysis.indicators.scope}`)
  lines.push(`Risk: ${analysis.indicators.risk}`)
  lines.push(`Estimated files: ~${analysis.indicators.estimatedFiles}`)

  if (analysis.indicators.keywords.length > 0) {
    lines.push(`Keywords: ${analysis.indicators.keywords.join(', ')}`)
  }

  return lines.join('\n')
}
