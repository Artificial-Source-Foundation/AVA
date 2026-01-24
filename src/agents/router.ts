/**
 * Delta9 Agent Router
 *
 * Routes tasks to appropriate agents based on task description and context.
 * Supports automatic routing and explicit routing via task.routedTo.
 */

import type { Task } from '../types/mission.js'
import type { Delta9Config } from '../types/config.js'
import { loadConfig } from '../lib/config.js'

// =============================================================================
// Types
// =============================================================================

export type AgentType =
  | 'operator' // Default task executor
  | 'operator-complex' // Complex task executor
  | 'scout' // File/codebase exploration
  | 'intel' // Documentation/web research
  | 'strategist' // Architecture/design decisions
  | 'ui-ops' // UI/frontend specialist
  | 'scribe' // Documentation writer
  | 'optics' // Visual/accessibility review
  | 'qa' // Testing specialist
  | 'validator' // Task validation
  | 'patcher' // Quick fixes

export interface RoutingDecision {
  /** Selected agent type */
  agent: AgentType
  /** Confidence in the routing (0-1) */
  confidence: number
  /** Reason for the routing decision */
  reason: string
  /** Whether this was explicit or auto-routed */
  explicit: boolean
}

// =============================================================================
// Routing Patterns
// =============================================================================

interface RoutingPattern {
  agent: AgentType
  patterns: RegExp[]
  weight: number
}

const ROUTING_PATTERNS: RoutingPattern[] = [
  // UI/Frontend tasks
  {
    agent: 'ui-ops',
    patterns: [
      /\b(ui|frontend|component|style|css|tailwind|styling)\b/i,
      /\b(button|form|modal|dialog|layout|responsive)\b/i,
      /\b(react|vue|svelte|angular|nextjs|astro)\b/i,
      /\b(animation|transition|design|theme)\b/i,
    ],
    weight: 1.0,
  },

  // Testing tasks
  {
    agent: 'qa',
    patterns: [
      /\b(test|spec|coverage|jest|vitest|playwright)\b/i,
      /\b(unit test|integration test|e2e|end.to.end)\b/i,
      /\b(mock|stub|fixture|assertion)\b/i,
      /\b(testing|test suite|test case)\b/i,
    ],
    weight: 1.0,
  },

  // Documentation tasks
  {
    agent: 'scribe',
    patterns: [
      /\b(doc|readme|comment|jsdoc|tsdoc)\b/i,
      /\b(documentation|document|annotate)\b/i,
      /\b(explain|describe|write up)\b/i,
      /\b(api doc|changelog|release notes)\b/i,
    ],
    weight: 0.9,
  },

  // Research/Intel tasks
  {
    agent: 'intel',
    patterns: [
      /\b(research|investigate|analyze|study)\b/i,
      /\b(find|search|look up|discover)\b/i,
      /\b(api|library|package|dependency)\b/i,
      /\b(documentation|docs|reference)\b/i,
    ],
    weight: 0.8,
  },

  // Exploration/Scout tasks
  {
    agent: 'scout',
    patterns: [
      /\b(explore|scan|map|traverse)\b/i,
      /\b(find files|locate|search codebase)\b/i,
      /\b(structure|overview|layout)\b/i,
      /\b(understand|familiarize)\b/i,
    ],
    weight: 0.8,
  },

  // Architecture/Design tasks
  {
    agent: 'strategist',
    patterns: [
      /\b(architect|design|plan|strategy)\b/i,
      /\b(refactor|restructure|reorganize)\b/i,
      /\b(pattern|approach|solution)\b/i,
      /\b(scalable|maintainable|extensible)\b/i,
    ],
    weight: 0.9,
  },

  // Visual/Accessibility tasks
  {
    agent: 'optics',
    patterns: [
      /\b(accessibility|a11y|wcag|aria)\b/i,
      /\b(visual|screenshot|appearance)\b/i,
      /\b(contrast|color|font)\b/i,
      /\b(screen reader|keyboard navigation)\b/i,
    ],
    weight: 0.9,
  },

  // Quick fix tasks
  {
    agent: 'patcher',
    patterns: [
      /\b(fix typo|quick fix|small change)\b/i,
      /\b(rename|update constant|change value)\b/i,
      /\b(minor|trivial|simple fix)\b/i,
    ],
    weight: 0.7,
  },

  // Complex tasks
  {
    agent: 'operator-complex',
    patterns: [
      /\b(complex|significant|major|critical)\b/i,
      /\b(implement|build|create|develop)\b/i,
      /\b(feature|functionality|capability)\b/i,
      /\b(system|module|service)\b/i,
    ],
    weight: 0.6,
  },
]

// =============================================================================
// Routing Logic
// =============================================================================

/**
 * Calculate match score for a task against a routing pattern
 */
function calculatePatternScore(
  description: string,
  pattern: RoutingPattern
): number {
  let matchCount = 0

  for (const regex of pattern.patterns) {
    if (regex.test(description)) {
      matchCount++
    }
  }

  if (matchCount === 0) return 0

  // Score based on number of matches and pattern weight
  const baseScore = matchCount / pattern.patterns.length
  return baseScore * pattern.weight
}

/**
 * Route a task to the appropriate agent
 */
export function routeTask(task: Task, cwd?: string): RoutingDecision {
  // Check for explicit routing
  if (task.routedTo) {
    return {
      agent: task.routedTo as AgentType,
      confidence: 1.0,
      reason: `Explicitly routed to ${task.routedTo}`,
      explicit: true,
    }
  }

  const description = task.description.toLowerCase()

  // Calculate scores for each pattern
  const scores: Array<{ agent: AgentType; score: number }> = []

  for (const pattern of ROUTING_PATTERNS) {
    const score = calculatePatternScore(description, pattern)
    if (score > 0) {
      scores.push({ agent: pattern.agent, score })
    }
  }

  // Sort by score descending
  scores.sort((a, b) => b.score - a.score)

  // If we have a clear winner, use it
  if (scores.length > 0 && scores[0].score > 0.3) {
    const best = scores[0]
    return {
      agent: best.agent,
      confidence: Math.min(1.0, best.score),
      reason: `Auto-routed based on task description patterns`,
      explicit: false,
    }
  }

  // Check for task complexity
  const isComplex = detectComplexity(task, cwd)
  if (isComplex) {
    return {
      agent: 'operator-complex',
      confidence: 0.7,
      reason: 'Task appears complex based on acceptance criteria',
      explicit: false,
    }
  }

  // Default to standard operator
  return {
    agent: 'operator',
    confidence: 0.5,
    reason: 'No specific routing pattern matched, using default operator',
    explicit: false,
  }
}

/**
 * Detect if a task is complex based on its characteristics
 */
function detectComplexity(task: Task, cwd?: string): boolean {
  // Check acceptance criteria count
  if (task.acceptanceCriteria && task.acceptanceCriteria.length >= 5) {
    return true
  }

  // Check description length (longer = more complex)
  if (task.description.length > 300) {
    return true
  }

  // Check for complexity keywords
  const complexKeywords = [
    'significant',
    'major',
    'critical',
    'complex',
    'extensive',
    'thorough',
    'comprehensive',
  ]

  const hasComplexKeyword = complexKeywords.some((kw) =>
    task.description.toLowerCase().includes(kw)
  )

  if (hasComplexKeyword) {
    return true
  }

  // Check config for complexity detection settings
  if (cwd) {
    try {
      const config = loadConfig(cwd)
      if (config.support.strategist.invokeThreshold === 'simple') {
        return true // Lower threshold means more tasks considered complex
      }
    } catch {
      // Config not available, continue with defaults
    }
  }

  return false
}

/**
 * Get the model for a routed agent
 */
export function getAgentModel(
  agent: AgentType,
  config: Delta9Config
): string {
  switch (agent) {
    case 'operator':
      return config.operators.defaultModel

    case 'operator-complex':
      return config.operators.complexModel

    case 'validator':
      return config.validator.model

    case 'patcher':
      return config.patcher.model

    case 'scout':
      return config.support.scout.model

    case 'intel':
      return config.support.intel.model

    case 'strategist':
      return config.support.strategist.model

    case 'ui-ops':
      return config.support.uiOps.model

    case 'scribe':
      return config.support.scribe.model

    case 'optics':
      return config.support.optics.model

    case 'qa':
      return config.support.qa.model

    default:
      return config.operators.defaultModel
  }
}

/**
 * Check if an agent type is a support agent
 */
export function isSupportAgent(agent: AgentType): boolean {
  return [
    'scout',
    'intel',
    'strategist',
    'ui-ops',
    'scribe',
    'optics',
    'qa',
  ].includes(agent)
}

/**
 * Get all available support agents
 */
export function getSupportAgents(): AgentType[] {
  return ['scout', 'intel', 'strategist', 'ui-ops', 'scribe', 'optics', 'qa']
}

/**
 * Suggest support agents for a task
 *
 * Returns agents that might be helpful but aren't the primary route.
 */
export function suggestSupportAgents(task: Task): AgentType[] {
  const description = task.description.toLowerCase()
  const suggestions: AgentType[] = []

  // Check each pattern and suggest if there's a match
  for (const pattern of ROUTING_PATTERNS) {
    if (!isSupportAgent(pattern.agent)) continue

    const score = calculatePatternScore(description, pattern)
    if (score > 0.2) {
      suggestions.push(pattern.agent)
    }
  }

  return suggestions
}
