/**
 * Delta9 Category-Based Routing
 *
 * Routes tasks to categories with configured temperature and model settings.
 * Categories provide a higher-level abstraction over agent routing.
 *
 * Categories:
 * - planning: Strategic planning and architecture decisions
 * - coding: General code implementation
 * - testing: Test writing and QA
 * - documentation: Docs, comments, README
 * - research: Information gathering and lookup
 * - ui: Frontend and UI work
 * - refactoring: Code refactoring and cleanup
 * - bugfix: Bug fixing and debugging
 */

import type { RoutableAgent } from './task-router.js'
import { CONFIDENCE } from '../lib/confidence-levels.js'
import { loadConfig } from '../lib/config.js'

// =============================================================================
// Types
// =============================================================================

/** Task category types */
export type TaskCategory =
  | 'planning'
  | 'coding'
  | 'testing'
  | 'documentation'
  | 'research'
  | 'ui'
  | 'refactoring'
  | 'bugfix'

/** Category configuration */
export interface CategoryConfig {
  /** Display name */
  name: string
  /** Description */
  description: string
  /** Preferred model for this category */
  model: string
  /** Temperature setting (0-1) */
  temperature: number
  /** Preferred agent for this category */
  preferredAgent: RoutableAgent
  /** Fallback agents in order */
  fallbackAgents: RoutableAgent[]
  /** Budget priority (1-10, higher = more budget allowed) */
  budgetPriority: number
  /** Keywords that indicate this category */
  keywords: string[]
}

/** Category match result */
export interface CategoryMatch {
  /** Matched category */
  category: TaskCategory
  /** Match confidence (0-1) */
  confidence: number
  /** Why this category was matched */
  reason: string
  /** Keywords that matched */
  matchedKeywords: string[]
  /** Category configuration */
  config: CategoryConfig
}

/** Category routing result */
export interface CategoryRouteResult {
  /** Primary category */
  primary: CategoryMatch
  /** Secondary categories (if task spans multiple) */
  secondary: CategoryMatch[]
  /** Effective model to use */
  effectiveModel: string
  /** Effective temperature */
  effectiveTemperature: number
  /** Recommended agent */
  recommendedAgent: RoutableAgent
}

// =============================================================================
// Category Configuration Builder
// =============================================================================

/** Category keywords (static, don't depend on config) */
const CATEGORY_KEYWORDS: Record<TaskCategory, string[]> = {
  planning: [
    'plan',
    'design',
    'architect',
    'strategy',
    'approach',
    'system design',
    'blueprint',
    'roadmap',
    'structure',
    'how should',
    'what approach',
    'best way to',
  ],
  coding: [
    'implement',
    'create',
    'build',
    'add',
    'write code',
    'function',
    'class',
    'method',
    'module',
    'feature',
    'endpoint',
    'api',
    'service',
    'logic',
  ],
  testing: [
    'test',
    'spec',
    'coverage',
    'jest',
    'vitest',
    'playwright',
    'e2e',
    'unit test',
    'integration',
    'mock',
    'fixture',
    'assert',
    'expect',
    'describe',
    'it(',
    'should',
  ],
  documentation: [
    'document',
    'readme',
    'comment',
    'jsdoc',
    'tsdoc',
    'api doc',
    'changelog',
    'guide',
    'tutorial',
    'example',
    'explain',
    'describe',
    'write docs',
  ],
  research: [
    'research',
    'find',
    'search',
    'lookup',
    'investigate',
    'how does',
    'what is',
    'learn about',
    'understand',
    'best practice',
    'documentation',
    'example of',
  ],
  ui: [
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
    'layout',
    'responsive',
    'accessibility',
    'a11y',
    'design',
  ],
  refactoring: [
    'refactor',
    'restructure',
    'reorganize',
    'clean up',
    'optimize',
    'improve',
    'simplify',
    'extract',
    'inline',
    'rename',
    'move',
    'split',
    'merge',
    'consolidate',
  ],
  bugfix: [
    'fix',
    'bug',
    'error',
    'issue',
    'broken',
    'not working',
    'crash',
    'exception',
    'fail',
    'debug',
    'troubleshoot',
    'resolve',
    'correct',
    'repair',
    'patch',
  ],
}

/**
 * Build category configs from loaded configuration.
 * Models are pulled from config, allowing runtime customization.
 */
export function getCategoryConfigs(cwd?: string): Record<TaskCategory, CategoryConfig> {
  const config = loadConfig(cwd || process.cwd())

  return {
    planning: {
      name: 'Planning',
      description: 'Strategic planning, architecture decisions, and design',
      model: config.commander.model,
      temperature: 0.7,
      preferredAgent: 'operator-complex',
      fallbackAgents: ['operator', 'strategist'],
      budgetPriority: 9,
      keywords: CATEGORY_KEYWORDS.planning,
    },
    coding: {
      name: 'Coding',
      description: 'General code implementation and development',
      model: config.operators.tier2Model,
      temperature: 0.3,
      preferredAgent: 'operator',
      fallbackAgents: ['operator-complex', 'patcher'],
      budgetPriority: 7,
      keywords: CATEGORY_KEYWORDS.coding,
    },
    testing: {
      name: 'Testing',
      description: 'Test writing, QA, and verification',
      model: config.support.qa.model,
      temperature: 0.2,
      preferredAgent: 'qa',
      fallbackAgents: ['operator'],
      budgetPriority: 6,
      keywords: CATEGORY_KEYWORDS.testing,
    },
    documentation: {
      name: 'Documentation',
      description: 'Documentation, comments, and README files',
      model: config.support.scribe.model,
      temperature: 0.5,
      preferredAgent: 'scribe',
      fallbackAgents: ['operator'],
      budgetPriority: 4,
      keywords: CATEGORY_KEYWORDS.documentation,
    },
    research: {
      name: 'Research',
      description: 'Information gathering, lookup, and investigation',
      model: config.support.intel.model,
      temperature: 0.4,
      preferredAgent: 'intel',
      fallbackAgents: ['scout', 'strategist'],
      budgetPriority: 5,
      keywords: CATEGORY_KEYWORDS.research,
    },
    ui: {
      name: 'UI/Frontend',
      description: 'User interface and frontend development',
      model: config.support.uiOps.model,
      temperature: 0.4,
      preferredAgent: 'ui-ops',
      fallbackAgents: ['operator', 'operator-complex'],
      budgetPriority: 6,
      keywords: CATEGORY_KEYWORDS.ui,
    },
    refactoring: {
      name: 'Refactoring',
      description: 'Code refactoring, cleanup, and optimization',
      model: config.operators.tier3Model, // Complex tasks use tier 3 (Delta Force)
      temperature: 0.2,
      preferredAgent: 'operator-complex',
      fallbackAgents: ['operator'],
      budgetPriority: 8,
      keywords: CATEGORY_KEYWORDS.refactoring,
    },
    bugfix: {
      name: 'Bug Fix',
      description: 'Bug fixing, debugging, and error resolution',
      model: config.operators.tier2Model, // Standard bugs use tier 2 (Marine Sergeant)
      temperature: 0.2,
      preferredAgent: 'operator',
      fallbackAgents: ['patcher', 'operator-complex'],
      budgetPriority: 8,
      keywords: CATEGORY_KEYWORDS.bugfix,
    },
  }
}

/**
 * Default category configs (for backwards compatibility).
 * Uses process.cwd() to load config.
 * @deprecated Use getCategoryConfigs(cwd) for explicit path control
 */
export const DEFAULT_CATEGORY_CONFIGS: Record<TaskCategory, CategoryConfig> = getCategoryConfigs()

// =============================================================================
// Category Detection
// =============================================================================

/**
 * Detect task category from description
 */
export function detectCategory(
  taskDescription: string,
  customConfigs?: Partial<Record<TaskCategory, Partial<CategoryConfig>>>
): CategoryMatch[] {
  const text = taskDescription.toLowerCase()
  const matches: CategoryMatch[] = []

  // Merge custom configs with defaults
  const configs = mergeConfigs(customConfigs)

  // Score each category
  for (const [category, config] of Object.entries(configs)) {
    const matchedKeywords: string[] = []
    let score = 0

    for (const keyword of config.keywords) {
      if (text.includes(keyword.toLowerCase())) {
        matchedKeywords.push(keyword)
        // Weight longer keywords higher
        score += 1 + keyword.length / 10
      }
    }

    if (score > 0) {
      // Calculate confidence (max CONFIDENCE.MAX)
      const confidence = Math.min(CONFIDENCE.MAX, CONFIDENCE.LOW + score * 0.1)

      matches.push({
        category: category as TaskCategory,
        confidence,
        reason: generateMatchReason(category as TaskCategory, matchedKeywords),
        matchedKeywords,
        config,
      })
    }
  }

  // Sort by confidence descending
  matches.sort((a, b) => b.confidence - a.confidence)

  return matches
}

/**
 * Generate reason for category match
 */
function generateMatchReason(category: TaskCategory, keywords: string[]): string {
  const categoryNames: Record<TaskCategory, string> = {
    planning: 'planning/architecture',
    coding: 'code implementation',
    testing: 'testing/QA',
    documentation: 'documentation',
    research: 'research/lookup',
    ui: 'UI/frontend',
    refactoring: 'refactoring',
    bugfix: 'bug fixing',
  }

  const uniqueKeywords = [...new Set(keywords)].slice(0, 3)
  return `Task involves ${categoryNames[category]} (keywords: ${uniqueKeywords.join(', ')})`
}

// =============================================================================
// Category Routing
// =============================================================================

/**
 * Route task to category and get effective settings
 */
export function routeToCategory(
  taskDescription: string,
  customConfigs?: Partial<Record<TaskCategory, Partial<CategoryConfig>>>,
  overrides?: {
    forceCategory?: TaskCategory
    forceModel?: string
    forceTemperature?: number
  }
): CategoryRouteResult {
  // Force category if specified
  if (overrides?.forceCategory) {
    const configs = mergeConfigs(customConfigs)
    const config = configs[overrides.forceCategory]

    const primary: CategoryMatch = {
      category: overrides.forceCategory,
      confidence: 1.0,
      reason: 'Category explicitly specified',
      matchedKeywords: [],
      config,
    }

    return {
      primary,
      secondary: [],
      effectiveModel: overrides.forceModel || config.model,
      effectiveTemperature: overrides.forceTemperature ?? config.temperature,
      recommendedAgent: config.preferredAgent,
    }
  }

  // Detect categories
  const matches = detectCategory(taskDescription, customConfigs)

  // If no matches, default to coding
  if (matches.length === 0) {
    const configs = mergeConfigs(customConfigs)
    const defaultConfig = configs.coding

    const primary: CategoryMatch = {
      category: 'coding',
      confidence: CONFIDENCE.SECONDARY_THRESHOLD,
      reason: 'Default category (no specific keywords matched)',
      matchedKeywords: [],
      config: defaultConfig,
    }

    return {
      primary,
      secondary: [],
      effectiveModel: overrides?.forceModel || defaultConfig.model,
      effectiveTemperature: overrides?.forceTemperature ?? defaultConfig.temperature,
      recommendedAgent: defaultConfig.preferredAgent,
    }
  }

  const primary = matches[0]
  const secondary = matches.slice(1).filter((m) => m.confidence > CONFIDENCE.SECONDARY_THRESHOLD)

  // Calculate effective settings
  // If multiple categories, blend temperature slightly
  let effectiveTemperature = primary.config.temperature
  if (secondary.length > 0 && secondary[0].confidence > CONFIDENCE.BLEND_THRESHOLD) {
    // Blend with secondary category
    const blend = 0.8 * primary.config.temperature + 0.2 * secondary[0].config.temperature
    effectiveTemperature = Math.round(blend * 100) / 100
  }

  return {
    primary,
    secondary,
    effectiveModel: overrides?.forceModel || primary.config.model,
    effectiveTemperature: overrides?.forceTemperature ?? effectiveTemperature,
    recommendedAgent: primary.config.preferredAgent,
  }
}

// =============================================================================
// Configuration Helpers
// =============================================================================

/**
 * Merge custom configs with defaults
 */
function mergeConfigs(
  customConfigs?: Partial<Record<TaskCategory, Partial<CategoryConfig>>>,
  cwd?: string
): Record<TaskCategory, CategoryConfig> {
  const baseConfigs = cwd ? getCategoryConfigs(cwd) : DEFAULT_CATEGORY_CONFIGS
  const result = { ...baseConfigs }

  if (customConfigs) {
    for (const [category, customConfig] of Object.entries(customConfigs)) {
      if (customConfig && category in result) {
        result[category as TaskCategory] = {
          ...result[category as TaskCategory],
          ...customConfig,
          // Merge keywords (extend, don't replace)
          keywords: customConfig.keywords
            ? [...result[category as TaskCategory].keywords, ...customConfig.keywords]
            : result[category as TaskCategory].keywords,
        }
      }
    }
  }

  return result
}

/**
 * Get category config
 */
export function getCategoryConfig(
  category: TaskCategory,
  customConfigs?: Partial<Record<TaskCategory, Partial<CategoryConfig>>>,
  cwd?: string
): CategoryConfig {
  const configs = mergeConfigs(customConfigs, cwd)
  return configs[category]
}

/**
 * Get all categories
 */
export function getAllCategories(): TaskCategory[] {
  return Object.keys(CATEGORY_KEYWORDS) as TaskCategory[]
}

/**
 * Check if a string is a valid category
 */
export function isValidCategory(value: string): value is TaskCategory {
  return value in CATEGORY_KEYWORDS
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Describe category route result
 */
export function describeCategoryRoute(result: CategoryRouteResult): string {
  const lines: string[] = []

  lines.push(`Category: ${result.primary.config.name}`)
  lines.push(`Confidence: ${(result.primary.confidence * 100).toFixed(0)}%`)
  lines.push(`Reason: ${result.primary.reason}`)
  lines.push(`Model: ${result.effectiveModel}`)
  lines.push(`Temperature: ${result.effectiveTemperature}`)
  lines.push(`Recommended Agent: ${result.recommendedAgent}`)

  if (result.secondary.length > 0) {
    lines.push('')
    lines.push('Secondary Categories:')
    for (const match of result.secondary) {
      lines.push(`  - ${match.config.name} (${(match.confidence * 100).toFixed(0)}%)`)
    }
  }

  return lines.join('\n')
}

/**
 * Get budget allowance for category (0-1 scale based on priority)
 */
export function getCategoryBudgetAllowance(category: TaskCategory, cwd?: string): number {
  const configs = cwd ? getCategoryConfigs(cwd) : DEFAULT_CATEGORY_CONFIGS
  const config = configs[category]
  return config.budgetPriority / 10
}

/**
 * Get temperature range recommendation for category
 */
export function getCategoryTemperatureRange(
  category: TaskCategory,
  cwd?: string
): {
  min: number
  max: number
  recommended: number
} {
  const configs = cwd ? getCategoryConfigs(cwd) : DEFAULT_CATEGORY_CONFIGS
  const config = configs[category]

  // Different categories have different acceptable ranges
  const ranges: Record<TaskCategory, { min: number; max: number }> = {
    planning: { min: 0.5, max: 0.9 },
    coding: { min: 0.1, max: 0.5 },
    testing: { min: 0.1, max: 0.4 },
    documentation: { min: 0.3, max: 0.7 },
    research: { min: 0.2, max: 0.6 },
    ui: { min: 0.2, max: 0.6 },
    refactoring: { min: 0.1, max: 0.4 },
    bugfix: { min: 0.1, max: 0.3 },
  }

  return {
    ...ranges[category],
    recommended: config.temperature,
  }
}
