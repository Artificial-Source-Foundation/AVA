/**
 * Delta9 Adaptive Timeout Estimator
 *
 * Provides intelligent timeout estimation based on:
 * - Agent type and role
 * - Prompt complexity (word count, code blocks, multiple tasks)
 * - Configurable bounds (min/max timeouts)
 *
 * @module lib/timeout-estimator
 */

// =============================================================================
// Types
// =============================================================================

/** Agent timeout category */
export type AgentTimeoutCategory = 'scout' | 'intel' | 'operator' | 'validator' | 'oracle'

/** Timeout estimate result */
export interface TimeoutEstimate {
  /** Base timeout for agent category (ms) */
  baseMs: number
  /** Calculated multiplier based on complexity */
  multiplier: number
  /** Final timeout value (ms) */
  finalMs: number
  /** Human-readable reason for the estimate */
  reason: string
  /** Complexity factors detected */
  factors: string[]
}

/** Timeout estimation options */
export interface TimeoutEstimationOptions {
  /** Maximum allowed timeout (default: 600_000ms = 10 min) */
  maxTimeout?: number
  /** Minimum allowed timeout (default: 30_000ms = 30 sec) */
  minTimeout?: number
  /** Base timeout override (ms) - bypasses category lookup */
  baseTimeoutOverride?: number
}

// =============================================================================
// Constants
// =============================================================================

/**
 * Base timeouts by agent category
 *
 * These are starting points that get multiplied based on complexity.
 */
export const BASE_TIMEOUTS: Record<AgentTimeoutCategory, number> = {
  /** Scout/RECON - fast reconnaissance, should complete quickly */
  scout: 60_000, // 1 minute

  /** Intel/SIGINT - research tasks, need time to search and analyze */
  intel: 180_000, // 3 minutes

  /** Operator - code generation, most variable depending on task */
  operator: 300_000, // 5 minutes

  /** Validator/SENTINEL - verification, moderate time */
  validator: 120_000, // 2 minutes

  /** Oracle - council deliberation, needs time for reasoning */
  oracle: 90_000, // 1.5 minutes
}

/**
 * Default timeout bounds
 */
export const DEFAULT_MIN_TIMEOUT = 30_000 // 30 seconds
export const DEFAULT_MAX_TIMEOUT = 600_000 // 10 minutes

/**
 * Complexity thresholds
 */
export const COMPLEXITY_THRESHOLDS = {
  /** Word count for "medium" complexity */
  mediumPromptWords: 200,
  /** Word count for "long" complexity */
  longPromptWords: 500,
  /** Minimum numbered items to detect multiple tasks */
  multipleTasksThreshold: 3,
}

/**
 * Complexity multipliers
 */
export const COMPLEXITY_MULTIPLIERS = {
  /** Multiplier for long prompts (>500 words) */
  longPrompt: 1.5,
  /** Multiplier for medium prompts (>200 words) */
  mediumPrompt: 1.25,
  /** Multiplier for prompts with code blocks */
  hasCodeBlocks: 1.2,
  /** Multiplier for prompts with multiple tasks */
  multipleTasks: 1.5,
  /** Multiplier for prompts with file paths (likely file operations) */
  hasFilePaths: 1.1,
  /** Multiplier for prompts with technical keywords */
  hasTechnicalKeywords: 1.15,
}

// =============================================================================
// Agent Name Mapping
// =============================================================================

/** Map agent names to timeout categories */
const AGENT_CATEGORY_MAP: Record<string, AgentTimeoutCategory> = {
  // Scout/reconnaissance
  RECON: 'scout',
  SCOUT: 'scout',

  // Intelligence/research
  SIGINT: 'intel',
  INTEL: 'intel',

  // Strategic Advisors (6 advisors)
  CIPHER: 'oracle',
  VECTOR: 'oracle',
  APEX: 'oracle',
  AEGIS: 'oracle',
  RAZOR: 'oracle',
  ORACLE: 'oracle',

  // Validators
  SENTINEL: 'validator',
  VALIDATOR: 'validator',

  // Operators and specialists (7 support agents)
  OPERATOR: 'operator',
  SURGEON: 'operator',
  SCRIBE: 'operator',
  FACADE: 'operator',
  TACCOM: 'intel', // Strategic advice, similar to intel
}

// =============================================================================
// Core Functions
// =============================================================================

/**
 * Get timeout category for an agent name
 *
 * @param agentName - Agent codename (case-insensitive)
 * @returns Timeout category
 */
export function getTimeoutCategory(agentName: string): AgentTimeoutCategory {
  const normalized = agentName.toUpperCase()
  return AGENT_CATEGORY_MAP[normalized] || 'operator'
}

/**
 * Estimate timeout for an agent and prompt
 *
 * Analyzes the prompt to determine complexity and calculates
 * an appropriate timeout based on agent type and detected factors.
 *
 * @param agentType - Agent type or category
 * @param prompt - The prompt/task description
 * @param options - Estimation options
 * @returns Timeout estimate with reasoning
 *
 * @example
 * ```typescript
 * const estimate = estimateTimeout('operator', 'Fix the authentication bug in login.ts')
 * console.log(estimate.finalMs) // 300000 (base operator timeout)
 *
 * const complexEstimate = estimateTimeout('operator', `
 *   Implement the following features:
 *   1. Add user authentication
 *   2. Add session management
 *   3. Add password reset
 *   4. Add 2FA support
 *
 *   \`\`\`typescript
 *   // Example code structure
 *   class AuthService { ... }
 *   \`\`\`
 * `)
 * console.log(complexEstimate.finalMs) // ~540000 (with multipliers)
 * ```
 */
export function estimateTimeout(
  agentType: AgentTimeoutCategory | string,
  prompt: string,
  options: TimeoutEstimationOptions = {}
): TimeoutEstimate {
  const {
    maxTimeout = DEFAULT_MAX_TIMEOUT,
    minTimeout = DEFAULT_MIN_TIMEOUT,
    baseTimeoutOverride,
  } = options

  // Determine base timeout
  const category = isAgentCategory(agentType) ? agentType : getTimeoutCategory(agentType)
  const baseMs = baseTimeoutOverride ?? BASE_TIMEOUTS[category]

  // Analyze prompt complexity
  const { multiplier, factors } = analyzePromptComplexity(prompt)

  // Calculate final timeout with bounds
  const rawTimeout = baseMs * multiplier
  const finalMs = Math.min(maxTimeout, Math.max(minTimeout, rawTimeout))

  // Generate reason
  const reason = generateReason(category, factors, multiplier, finalMs, rawTimeout)

  return {
    baseMs,
    multiplier,
    finalMs,
    reason,
    factors,
  }
}

/**
 * Quick timeout estimate for known agent
 *
 * @param agentName - Agent codename
 * @param prompt - The prompt
 * @returns Timeout in milliseconds
 */
export function quickEstimate(agentName: string, prompt: string): number {
  return estimateTimeout(agentName, prompt).finalMs
}

/**
 * Get base timeout for an agent category
 *
 * @param category - Agent category or name
 * @returns Base timeout in milliseconds
 */
export function getBaseTimeout(category: AgentTimeoutCategory | string): number {
  if (isAgentCategory(category)) {
    return BASE_TIMEOUTS[category]
  }
  return BASE_TIMEOUTS[getTimeoutCategory(category)]
}

// =============================================================================
// Internal Functions
// =============================================================================

/**
 * Check if value is a valid agent category
 */
function isAgentCategory(value: string): value is AgentTimeoutCategory {
  return value in BASE_TIMEOUTS
}

/**
 * Analyze prompt complexity and return multiplier
 */
function analyzePromptComplexity(prompt: string): { multiplier: number; factors: string[] } {
  const factors: string[] = []
  let multiplier = 1.0

  // Word count analysis
  const words = prompt.split(/\s+/).filter((w) => w.length > 0)
  const wordCount = words.length

  if (wordCount > COMPLEXITY_THRESHOLDS.longPromptWords) {
    multiplier *= COMPLEXITY_MULTIPLIERS.longPrompt
    factors.push(`long prompt (${wordCount} words)`)
  } else if (wordCount > COMPLEXITY_THRESHOLDS.mediumPromptWords) {
    multiplier *= COMPLEXITY_MULTIPLIERS.mediumPrompt
    factors.push(`medium prompt (${wordCount} words)`)
  }

  // Code blocks detection
  const codeBlockCount = (prompt.match(/```/g) || []).length / 2
  if (codeBlockCount > 0) {
    multiplier *= COMPLEXITY_MULTIPLIERS.hasCodeBlocks
    factors.push(`${Math.floor(codeBlockCount)} code block(s)`)
  }

  // Multiple tasks detection (numbered lists)
  const numberedItems = (prompt.match(/^\s*\d+\./gm) || []).length
  if (numberedItems >= COMPLEXITY_THRESHOLDS.multipleTasksThreshold) {
    multiplier *= COMPLEXITY_MULTIPLIERS.multipleTasks
    factors.push(`${numberedItems} numbered tasks`)
  }

  // File path detection
  const hasFilePaths =
    /(?:\/[\w.-]+)+\.[\w]+/g.test(prompt) || /[\w.-]+\.(?:ts|js|py|md|json)/g.test(prompt)
  if (hasFilePaths) {
    multiplier *= COMPLEXITY_MULTIPLIERS.hasFilePaths
    factors.push('file operations')
  }

  // Technical keyword detection
  const technicalKeywords = [
    'refactor',
    'implement',
    'migrate',
    'architecture',
    'performance',
    'security',
    'integration',
    'database',
    'api',
    'authentication',
  ]
  const foundKeywords = technicalKeywords.filter((kw) => prompt.toLowerCase().includes(kw))
  if (foundKeywords.length >= 2) {
    multiplier *= COMPLEXITY_MULTIPLIERS.hasTechnicalKeywords
    factors.push(`technical scope (${foundKeywords.slice(0, 3).join(', ')})`)
  }

  return { multiplier, factors }
}

/**
 * Generate human-readable reason for timeout estimate
 */
function generateReason(
  category: AgentTimeoutCategory,
  factors: string[],
  multiplier: number,
  finalMs: number,
  rawTimeout: number
): string {
  const parts: string[] = []

  // Base reason
  parts.push(`${category} agent base timeout`)

  // Complexity factors
  if (factors.length > 0) {
    parts.push(`+ complexity factors: ${factors.join(', ')}`)
  }

  // Multiplier
  if (multiplier !== 1.0) {
    parts.push(`(${multiplier.toFixed(2)}x multiplier)`)
  }

  // Bounds adjustment
  if (finalMs !== rawTimeout) {
    if (finalMs < rawTimeout) {
      parts.push('[capped at max]')
    } else {
      parts.push('[raised to min]')
    }
  }

  return parts.join(' ')
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Format timeout for display
 *
 * @param ms - Timeout in milliseconds
 * @returns Formatted string (e.g., "2m 30s")
 */
export function formatTimeout(ms: number): string {
  const seconds = Math.floor(ms / 1000)
  const minutes = Math.floor(seconds / 60)
  const remainingSeconds = seconds % 60

  if (minutes === 0) {
    return `${seconds}s`
  }

  if (remainingSeconds === 0) {
    return `${minutes}m`
  }

  return `${minutes}m ${remainingSeconds}s`
}

/**
 * Parse timeout string to milliseconds
 *
 * @param str - Timeout string (e.g., "5m", "120s", "2m30s")
 * @returns Milliseconds or undefined if invalid
 */
export function parseTimeoutString(str: string): number | undefined {
  const minuteMatch = str.match(/(\d+)\s*m/i)
  const secondMatch = str.match(/(\d+)\s*s/i)

  if (!minuteMatch && !secondMatch) {
    return undefined
  }

  let ms = 0
  if (minuteMatch) {
    ms += parseInt(minuteMatch[1], 10) * 60 * 1000
  }
  if (secondMatch) {
    ms += parseInt(secondMatch[1], 10) * 1000
  }

  return ms
}

/**
 * Get all available agent categories with their base timeouts
 */
export function getAllCategories(): Array<{
  category: AgentTimeoutCategory
  baseMs: number
  formatted: string
}> {
  return Object.entries(BASE_TIMEOUTS).map(([category, baseMs]) => ({
    category: category as AgentTimeoutCategory,
    baseMs,
    formatted: formatTimeout(baseMs),
  }))
}
