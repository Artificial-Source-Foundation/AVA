/**
 * Delta9 Task Router
 *
 * Routes tasks to the most appropriate agent based on:
 * - Task type keywords
 * - Complexity analysis
 * - Required capabilities
 * - Current context
 *
 * Delta Team Support Agents (config key → codename):
 * - scout → RECON (reconnaissance)
 * - intel → SIGINT (intelligence research)
 * - strategist → TACCOM (tactical command)
 * - patcher → SURGEON (surgical fixes)
 * - qa → SENTINEL (quality assurance)
 * - scribe → SCRIBE (documentation)
 * - ui-ops → FACADE (frontend operations + visual tasks)
 *
 * Note: SPECTRE (optics) removed - visual tasks merged into FACADE
 * Note: Actual models come from config, AGENT_MODELS are fallback defaults.
 */

import { analyzeComplexity, type ComplexityAnalysis } from './complexity.js'
import type { Complexity } from '../types/mission.js'
import { loadConfig } from '../lib/config.js'

// =============================================================================
// Types
// =============================================================================

/** Agent types that can be routed to */
export type RoutableAgent =
  | 'operator' // Alias for operator-tier2 (backward compat)
  | 'operator-complex' // Alias for operator-tier3 (backward compat)
  | 'operator-tier1' // Marine Private: Simple tasks
  | 'operator-tier2' // Marine Sergeant: Moderate tasks
  | 'operator-tier3' // Delta Force: Critical/complex tasks
  | 'patcher'
  | 'scout'
  | 'intel'
  | 'strategist'
  | 'ui-ops'
  | 'scribe'
  | 'qa'

/** Routing decision */
export interface RouteDecision {
  /** Recommended agent */
  agent: RoutableAgent
  /** Default model for this agent */
  model: string
  /** Why this agent was chosen */
  reason: string
  /** Confidence in this routing (0-1) */
  confidence: number
  /** Fallback agent if primary unavailable */
  fallbackAgent?: RoutableAgent
  /** Additional routing metadata */
  metadata?: {
    matchedKeywords?: string[]
    complexity?: Complexity
    capabilities?: string[]
  }
}

/** Input for task routing */
export interface TaskRouterInput {
  /** Task description */
  taskDescription: string
  /** Explicit task type (optional) */
  taskType?: string
  /** Pre-computed complexity analysis (optional) */
  complexity?: ComplexityAnalysis
  /** Working directory for config loading */
  cwd?: string
  /** Additional context */
  context?: {
    /** Previous task failures */
    previousFailures?: number
    /** Budget constraints (prefer cheaper agents) */
    budgetConstrained?: boolean
    /** Files involved */
    files?: string[]
    /** Mission phase */
    phase?: 'planning' | 'execution' | 'validation'
  }
}

// =============================================================================
// Keyword Patterns
// =============================================================================

/** Keywords mapped to agents */
const AGENT_KEYWORDS: Record<RoutableAgent, string[]> = {
  'ui-ops': [
    'ui',
    'frontend',
    'component',
    'style',
    'css',
    'tailwind',
    'react',
    'vue',
    'svelte',
    'button',
    'form',
    'modal',
    'dialog',
    'layout',
    'responsive',
    'accessibility',
    'a11y',
    'aria',
    // Visual keywords (merged from optics/SPECTRE)
    'image',
    'screenshot',
    'diagram',
    'visual',
    'picture',
    'photo',
    'pdf',
    'mockup',
  ],
  qa: [
    'test',
    'spec',
    'coverage',
    'jest',
    'vitest',
    'playwright',
    'cypress',
    'e2e',
    'unit test',
    'integration test',
    'mock',
    'fixture',
    'assertion',
    'expect',
    'describe',
    'it(',
  ],
  scribe: [
    'doc',
    'readme',
    'documentation',
    'comment',
    'jsdoc',
    'tsdoc',
    'api doc',
    'changelog',
    'guide',
    'tutorial',
    'example',
  ],
  scout: [
    'search',
    'find',
    'grep',
    'locate',
    'where',
    'which file',
    'look for',
    'pattern',
    'usage',
    'reference',
    'import',
  ],
  intel: [
    'research',
    'lookup',
    'library',
    'package',
    'npm',
    'how to',
    'best practice',
    'documentation',
    'official docs',
    'example from',
  ],
  strategist: [
    'stuck',
    'blocked',
    'help',
    'advice',
    'guidance',
    'not working',
    'tried everything',
    "don't know",
    'confused',
    'approach',
    'alternative',
    'second opinion',
    'getting complex',
  ],
  // Note: Visual keywords merged into ui-ops since SPECTRE was removed
  patcher: [
    'typo',
    'fix typo',
    'rename',
    'simple fix',
    'quick fix',
    'small change',
    'one line',
    'minor',
    'trivial',
    'lint',
  ],
  // 3-Tier Operator System (Marine hierarchy)
  'operator-tier1': [
    // Marine Private: Simple tasks
    'typo',
    'fix',
    'simple',
    'quick',
    'rename',
    'style',
    'format',
    'minor',
    'tweak',
    'adjust',
  ],
  'operator-tier2': [
    // Marine Sergeant: Moderate tasks (default operator)
    'add',
    'implement',
    'update',
    'create',
    'extend',
    'component',
    'endpoint',
    'feature',
    'method',
    'function',
  ],
  'operator-tier3': [
    // Delta Force: Critical/complex tasks
    'refactor',
    'rewrite',
    'migrate',
    'architecture',
    'redesign',
    'complex',
    'multiple files',
    'system',
    'comprehensive',
    'overhaul',
  ],
  'operator-complex': [
    // Alias for operator-tier3 (backward compat)
    'refactor',
    'rewrite',
    'migrate',
    'architecture',
    'redesign',
    'complex',
    'multiple files',
    'system',
    'comprehensive',
  ],
  operator: [], // Alias for operator-tier2 (default - no specific keywords)
}

/**
 * Get agent model from config
 *
 * Models are loaded from config, with hardcoded fallbacks only if config fails.
 */
function getAgentModel(agent: RoutableAgent, cwd?: string): string {
  try {
    const config = loadConfig(cwd || process.cwd())

    switch (agent) {
      // 3-tier operator system (Marine hierarchy)
      case 'operator-tier1':
        return config.operators.tier1Model // Marine Private (simple)
      case 'operator':
      case 'operator-tier2':
        return config.operators.tier2Model // Marine Sergeant (default)
      case 'operator-complex':
      case 'operator-tier3':
        return config.operators.tier3Model // Delta Force (critical)
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
      case 'qa':
        return config.support.qa.model
      default:
        return config.operators.tier2Model
    }
  } catch {
    // Fallback defaults if config loading fails
    const FALLBACK_MODELS: Record<RoutableAgent, string> = {
      operator: 'anthropic/claude-sonnet-4-5',
      'operator-complex': 'anthropic/claude-opus-4-5',
      'operator-tier1': 'anthropic/claude-sonnet-4-5', // Marine Private
      'operator-tier2': 'openai/gpt-5.2-codex', // Marine Sergeant
      'operator-tier3': 'anthropic/claude-opus-4-5', // Delta Force
      patcher: 'anthropic/claude-haiku-4',
      scout: 'anthropic/claude-haiku-4',
      intel: 'anthropic/claude-sonnet-4-5',
      strategist: 'openai/gpt-4o',
      'ui-ops': 'google/gemini-2.5-flash',
      scribe: 'google/gemini-2.5-flash',
      qa: 'anthropic/claude-sonnet-4-5',
    }
    return FALLBACK_MODELS[agent]
  }
}

/** Agent fallbacks (tier escalation/de-escalation) */
const AGENT_FALLBACKS: Partial<Record<RoutableAgent, RoutableAgent>> = {
  'ui-ops': 'operator-tier2',
  qa: 'operator-tier2',
  scribe: 'operator-tier2',
  patcher: 'operator-tier1',
  'operator-tier1': 'operator-tier2', // Escalate simple to moderate
  'operator-tier2': 'operator-tier3', // Escalate moderate to critical
  'operator-tier3': 'operator-tier2', // De-escalate if critical fails
  operator: 'operator-tier2',
  'operator-complex': 'operator-tier3',
  strategist: 'intel',
  intel: 'scout',
}

// =============================================================================
// Router Implementation
// =============================================================================

/**
 * Route a task to the most appropriate agent
 */
export function routeTask(input: TaskRouterInput): RouteDecision {
  const { taskDescription, taskType, context } = input
  const text = `${taskDescription} ${taskType || ''}`.toLowerCase()

  // Track matched keywords for confidence calculation
  const matchedKeywords: string[] = []
  const agentScores: Map<RoutableAgent, number> = new Map()

  // Initialize all agents with 0 score
  for (const agent of Object.keys(AGENT_KEYWORDS) as RoutableAgent[]) {
    agentScores.set(agent, 0)
  }

  // Score agents based on keyword matches
  for (const [agent, keywords] of Object.entries(AGENT_KEYWORDS)) {
    for (const keyword of keywords) {
      if (text.includes(keyword)) {
        const currentScore = agentScores.get(agent as RoutableAgent) || 0
        agentScores.set(agent as RoutableAgent, currentScore + 1)
        matchedKeywords.push(keyword)
      }
    }
  }

  // Get complexity analysis if not provided
  const complexity = input.complexity || analyzeComplexity(taskDescription)

  // Apply complexity modifiers
  if (complexity.complexity === 'critical' || complexity.complexity === 'high') {
    const complexScore = agentScores.get('operator-complex') || 0
    agentScores.set('operator-complex', complexScore + 2)
  }

  if (complexity.complexity === 'low') {
    const patcherScore = agentScores.get('patcher') || 0
    agentScores.set('patcher', patcherScore + 1)
  }

  // Apply context modifiers
  if (context?.previousFailures && context.previousFailures > 0) {
    // If previous failures, suggest Strategist
    const strategistScore = agentScores.get('strategist') || 0
    agentScores.set('strategist', strategistScore + context.previousFailures)
  }

  if (context?.budgetConstrained) {
    // Prefer cheaper agents
    const patcherScore = agentScores.get('patcher') || 0
    const scoutScore = agentScores.get('scout') || 0
    agentScores.set('patcher', patcherScore + 1)
    agentScores.set('scout', scoutScore + 1)
  }

  // Find best agent
  let bestAgent: RoutableAgent = 'operator'
  let bestScore = 0

  for (const [agent, score] of agentScores) {
    if (score > bestScore) {
      bestScore = score
      bestAgent = agent
    }
  }

  // Calculate confidence
  const totalKeywords = matchedKeywords.length
  const confidence =
    bestScore > 0 ? Math.min(0.95, 0.5 + bestScore * 0.1 + totalKeywords * 0.05) : 0.4 // Low confidence for default routing

  // Generate reason
  const reason = generateReason(bestAgent, matchedKeywords, complexity, context)

  return {
    agent: bestAgent,
    model: getAgentModel(bestAgent, input.cwd),
    reason,
    confidence,
    fallbackAgent: AGENT_FALLBACKS[bestAgent],
    metadata: {
      matchedKeywords: [...new Set(matchedKeywords)].slice(0, 5),
      complexity: complexity.complexity,
      capabilities: getAgentCapabilities(bestAgent),
    },
  }
}

/**
 * Generate human-readable reason for routing decision
 */
function generateReason(
  agent: RoutableAgent,
  keywords: string[],
  complexity: ComplexityAnalysis,
  context?: TaskRouterInput['context']
): string {
  const parts: string[] = []

  // Agent-specific reasons
  switch (agent) {
    case 'ui-ops':
      parts.push('Frontend/UI task detected')
      break
    case 'qa':
      parts.push('Testing task detected')
      break
    case 'scribe':
      parts.push('Documentation task detected')
      break
    case 'scout':
      parts.push('Codebase search requested')
      break
    case 'intel':
      parts.push('Research/documentation lookup needed')
      break
    case 'strategist':
      parts.push('Guidance/advice requested')
      break
    case 'patcher':
      parts.push('Simple/quick fix detected')
      break
    case 'operator-tier1':
      parts.push('Simple task (Marine Private tier)')
      break
    case 'operator':
    case 'operator-tier2':
      parts.push('Moderate implementation task (Marine Sergeant tier)')
      break
    case 'operator-complex':
    case 'operator-tier3':
      parts.push('Complex/critical task (Delta Force tier)')
      break
  }

  // Keyword mentions
  if (keywords.length > 0) {
    const uniqueKeywords = [...new Set(keywords)].slice(0, 3)
    parts.push(`Keywords: ${uniqueKeywords.join(', ')}`)
  }

  // Complexity mention
  if (complexity.complexity !== 'medium') {
    parts.push(`Complexity: ${complexity.complexity}`)
  }

  // Context mentions
  if (context?.previousFailures) {
    parts.push(`Previous failures: ${context.previousFailures}`)
  }

  return parts.join('. ')
}

/**
 * Get capabilities for an agent
 */
function getAgentCapabilities(agent: RoutableAgent): string[] {
  switch (agent) {
    case 'operator-tier1':
      return ['code-modification', 'simple-fixes', 'formatting', 'renaming']
    case 'operator':
    case 'operator-tier2':
      return ['code-modification', 'implementation', 'general-tasks', 'features']
    case 'operator-complex':
    case 'operator-tier3':
      return ['code-modification', 'multi-file', 'refactoring', 'architecture', 'critical-changes']
    case 'patcher':
      return ['code-modification', 'quick-fixes', 'simple-changes']
    case 'scout':
      return ['codebase-search', 'pattern-matching', 'file-discovery']
    case 'intel':
      return ['web-search', 'documentation', 'research']
    case 'strategist':
      return ['advice', 'problem-solving', 'alternative-approaches']
    case 'ui-ops':
      return ['code-modification', 'frontend', 'components', 'styling', 'image-analysis']
    case 'scribe':
      return ['documentation', 'code-modification', 'comments']
    case 'qa':
      return ['code-modification', 'testing', 'coverage']
  }
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Check if an agent can modify files
 */
export function canAgentModifyFiles(agent: RoutableAgent): boolean {
  const modifyAgents: RoutableAgent[] = [
    'operator',
    'operator-complex',
    'operator-tier1',
    'operator-tier2',
    'operator-tier3',
    'patcher',
    'ui-ops',
    'scribe',
    'qa',
  ]
  return modifyAgents.includes(agent)
}

/**
 * Check if an agent is a support agent (not execution)
 */
export function isSupportAgent(agent: RoutableAgent): boolean {
  const supportAgents: RoutableAgent[] = ['scout', 'intel', 'strategist']
  return supportAgents.includes(agent)
}

/**
 * Get all available agents
 */
export function getAvailableAgents(): RoutableAgent[] {
  return Object.keys(AGENT_KEYWORDS) as RoutableAgent[]
}

/**
 * Describe routing decision in human-readable format
 */
export function describeRouteDecision(decision: RouteDecision): string {
  const lines: string[] = []

  lines.push(`Recommended Agent: ${decision.agent.toUpperCase()}`)
  lines.push(`Model: ${decision.model}`)
  lines.push(`Confidence: ${(decision.confidence * 100).toFixed(0)}%`)
  lines.push(`Reason: ${decision.reason}`)

  if (decision.fallbackAgent) {
    lines.push(`Fallback: ${decision.fallbackAgent}`)
  }

  if (decision.metadata?.matchedKeywords?.length) {
    lines.push(`Matched Keywords: ${decision.metadata.matchedKeywords.join(', ')}`)
  }

  if (decision.metadata?.capabilities?.length) {
    lines.push(`Capabilities: ${decision.metadata.capabilities.join(', ')}`)
  }

  return lines.join('\n')
}
