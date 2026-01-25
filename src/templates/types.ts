/**
 * Delta9 Mission Template Types
 *
 * Type definitions for mission templates.
 */

import type { Complexity, Objective } from '../types/mission.js'
import type { CouncilMode } from '../types/config.js'

// =============================================================================
// Template Types
// =============================================================================

/** Template task definition (without runtime fields) */
export interface TemplateTask {
  /** Task description placeholder */
  description: string
  /** Default acceptance criteria */
  acceptanceCriteria: string[]
  /** Suggested specialist routing */
  routeTo?: string
  /** Dependencies on other tasks (by index within objective) */
  dependsOn?: number[]
}

/** Template objective definition */
export interface TemplateObjective {
  /** Objective description placeholder */
  description: string
  /** Tasks within this objective */
  tasks: TemplateTask[]
}

/** Mission template definition */
export interface MissionTemplate {
  /** Template name */
  name: string
  /** Template description */
  description: string
  /** Template type identifier */
  type: 'feature' | 'bugfix' | 'refactor' | 'migration' | 'documentation' | 'testing' | 'custom'
  /** Default complexity */
  defaultComplexity: Complexity
  /** Suggested council mode */
  suggestedCouncilMode: CouncilMode
  /** Template objectives */
  objectives: TemplateObjective[]
  /** Variables that should be filled in */
  variables: TemplateVariable[]
  /** Tags for categorization */
  tags: string[]
}

/** Variable placeholder in template */
export interface TemplateVariable {
  /** Variable name (e.g., {{FEATURE_NAME}}) */
  name: string
  /** Description of what to fill in */
  description: string
  /** Whether this variable is required */
  required: boolean
  /** Example value */
  example?: string
}

/** Template instantiation options */
export interface TemplateInstantiationOptions {
  /** Values for template variables */
  variables: Record<string, string>
  /** Override complexity */
  complexity?: Complexity
  /** Override council mode */
  councilMode?: CouncilMode
  /** Additional acceptance criteria to add */
  additionalCriteria?: string[]
}

/** Result of template instantiation */
export interface InstantiatedTemplate {
  /** Mission description */
  description: string
  /** Complexity */
  complexity: Complexity
  /** Council mode */
  councilMode: CouncilMode
  /** Objectives with tasks */
  objectives: Objective[]
}
