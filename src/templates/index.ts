/**
 * Delta9 Mission Templates
 *
 * Pre-built mission structures for common development tasks.
 * Templates provide default objectives, tasks, and acceptance criteria.
 */

import type {
  MissionTemplate,
  TemplateInstantiationOptions,
  InstantiatedTemplate,
} from './types.js'
import type { Objective, Task, Complexity } from '../types/mission.js'

// =============================================================================
// Template Exports
// =============================================================================

// Types
export type {
  MissionTemplate,
  TemplateInstantiationOptions,
  InstantiatedTemplate,
  TemplateVariable,
} from './types.js'

// Feature templates
export { featureTemplate, simpleFeatureTemplate, complexFeatureTemplate } from './feature.js'

// Bugfix templates
export {
  bugfixTemplate,
  quickBugfixTemplate,
  criticalBugfixTemplate,
  securityBugfixTemplate,
} from './bugfix.js'

// Refactor templates
export {
  refactorTemplate,
  quickRefactorTemplate,
  largeRefactorTemplate,
  performanceRefactorTemplate,
  typeSafetyRefactorTemplate,
} from './refactor.js'

// =============================================================================
// Template Registry
// =============================================================================

import { featureTemplate, simpleFeatureTemplate, complexFeatureTemplate } from './feature.js'
import {
  bugfixTemplate,
  quickBugfixTemplate,
  criticalBugfixTemplate,
  securityBugfixTemplate,
} from './bugfix.js'
import {
  refactorTemplate,
  quickRefactorTemplate,
  largeRefactorTemplate,
  performanceRefactorTemplate,
  typeSafetyRefactorTemplate,
} from './refactor.js'

/** All available templates indexed by ID */
export const templateRegistry: Record<string, MissionTemplate> = {
  // Feature templates
  feature: featureTemplate,
  'feature:simple': simpleFeatureTemplate,
  'feature:complex': complexFeatureTemplate,

  // Bugfix templates
  bugfix: bugfixTemplate,
  'bugfix:quick': quickBugfixTemplate,
  'bugfix:critical': criticalBugfixTemplate,
  'bugfix:security': securityBugfixTemplate,

  // Refactor templates
  refactor: refactorTemplate,
  'refactor:quick': quickRefactorTemplate,
  'refactor:large': largeRefactorTemplate,
  'refactor:performance': performanceRefactorTemplate,
  'refactor:types': typeSafetyRefactorTemplate,
}

// =============================================================================
// Template Functions
// =============================================================================

/**
 * Get a template by ID
 */
export function getTemplate(templateId: string): MissionTemplate | undefined {
  return templateRegistry[templateId]
}

/**
 * List all available templates
 */
export function listTemplates(): Array<{
  id: string
  name: string
  type: MissionTemplate['type']
  description: string
  complexity: Complexity
  tags: string[]
}> {
  return Object.entries(templateRegistry).map(([id, template]) => ({
    id,
    name: template.name,
    type: template.type,
    description: template.description,
    complexity: template.defaultComplexity,
    tags: template.tags,
  }))
}

/**
 * Find templates by type
 */
export function findTemplatesByType(type: MissionTemplate['type']): MissionTemplate[] {
  return Object.values(templateRegistry).filter((t) => t.type === type)
}

/**
 * Find templates by tag
 */
export function findTemplatesByTag(tag: string): MissionTemplate[] {
  return Object.values(templateRegistry).filter((t) => t.tags.includes(tag))
}

/**
 * Suggest a template based on description
 */
export function suggestTemplate(description: string): {
  template: MissionTemplate
  templateId: string
  confidence: number
} | null {
  const lowerDesc = description.toLowerCase()

  // Scoring based on keywords
  const scores: Array<{ id: string; template: MissionTemplate; score: number }> = []

  for (const [id, template] of Object.entries(templateRegistry)) {
    let score = 0

    // Check template type keywords
    if (
      template.type === 'bugfix' &&
      (lowerDesc.includes('bug') ||
        lowerDesc.includes('fix') ||
        lowerDesc.includes('issue') ||
        lowerDesc.includes('error'))
    ) {
      score += 30
    }
    if (
      template.type === 'feature' &&
      (lowerDesc.includes('feature') ||
        lowerDesc.includes('add') ||
        lowerDesc.includes('implement') ||
        lowerDesc.includes('create') ||
        lowerDesc.includes('new'))
    ) {
      score += 30
    }
    if (
      template.type === 'refactor' &&
      (lowerDesc.includes('refactor') ||
        lowerDesc.includes('clean') ||
        lowerDesc.includes('improve') ||
        lowerDesc.includes('reorganize'))
    ) {
      score += 30
    }

    // Check for complexity indicators
    if (id.includes('quick') || id.includes('simple')) {
      if (
        lowerDesc.includes('quick') ||
        lowerDesc.includes('simple') ||
        lowerDesc.includes('small') ||
        lowerDesc.length < 50
      ) {
        score += 20
      }
    }
    if (id.includes('complex') || id.includes('large') || id.includes('critical')) {
      if (
        lowerDesc.includes('complex') ||
        lowerDesc.includes('large') ||
        lowerDesc.includes('critical') ||
        lowerDesc.includes('major')
      ) {
        score += 20
      }
    }

    // Check specific keywords
    if (
      id.includes('security') &&
      (lowerDesc.includes('security') ||
        lowerDesc.includes('vulnerability') ||
        lowerDesc.includes('cve'))
    ) {
      score += 25
    }
    if (
      id.includes('performance') &&
      (lowerDesc.includes('performance') ||
        lowerDesc.includes('slow') ||
        lowerDesc.includes('optimize') ||
        lowerDesc.includes('speed'))
    ) {
      score += 25
    }
    if (
      id.includes('types') &&
      (lowerDesc.includes('type') || lowerDesc.includes('typescript') || lowerDesc.includes('any'))
    ) {
      score += 25
    }

    // Check tag matches
    for (const tag of template.tags) {
      if (lowerDesc.includes(tag)) {
        score += 5
      }
    }

    if (score > 0) {
      scores.push({ id, template, score })
    }
  }

  if (scores.length === 0) {
    return null
  }

  // Return highest scoring template
  scores.sort((a, b) => b.score - a.score)
  const best = scores[0]

  // Require minimum score of 25 to return a suggestion
  // This prevents weak matches from short descriptions
  if (best.score < 25) {
    return null
  }

  return {
    template: best.template,
    templateId: best.id,
    confidence: Math.min(best.score / 100, 1),
  }
}

// =============================================================================
// Template Instantiation
// =============================================================================

/**
 * Replace variables in a string
 */
function replaceVariables(text: string, variables: Record<string, string>): string {
  let result = text
  for (const [name, value] of Object.entries(variables)) {
    result = result.replace(new RegExp(name.replace(/[{}]/g, '\\$&'), 'g'), value)
  }
  return result
}

/**
 * Generate a unique task ID
 */
function generateTaskId(objectiveIndex: number, taskIndex: number): string {
  return `task_${objectiveIndex + 1}_${taskIndex + 1}_${Date.now()}`
}

/**
 * Generate a unique objective ID
 */
function generateObjectiveId(index: number): string {
  return `obj_${index + 1}_${Date.now()}`
}

/**
 * Instantiate a template with provided variables
 */
export function instantiateTemplate(
  template: MissionTemplate,
  options: TemplateInstantiationOptions
): InstantiatedTemplate {
  const { variables, complexity, councilMode, additionalCriteria } = options

  // Validate required variables
  for (const variable of template.variables) {
    if (variable.required && !variables[variable.name]) {
      throw new Error(`Required variable ${variable.name} not provided`)
    }
  }

  // Create objectives with tasks
  const objectives: Objective[] = template.objectives.map((templateObj, objIndex) => {
    const objectiveId = generateObjectiveId(objIndex)

    // Create tasks for this objective
    const tasks: Task[] = templateObj.tasks.map((templateTask, taskIndex) => {
      // Build acceptance criteria
      let criteria = templateTask.acceptanceCriteria.map((c) => replaceVariables(c, variables))

      // Add additional criteria if this is the last task
      if (
        additionalCriteria &&
        objIndex === template.objectives.length - 1 &&
        taskIndex === templateObj.tasks.length - 1
      ) {
        criteria = [...criteria, ...additionalCriteria]
      }

      // Build dependencies (convert relative to absolute IDs)
      const dependencies = templateTask.dependsOn?.map(
        (depIndex) => `task_${objIndex + 1}_${depIndex + 1}_${Date.now()}`
      )

      return {
        id: generateTaskId(objIndex, taskIndex),
        description: replaceVariables(templateTask.description, variables),
        status: 'pending' as const,
        attempts: 0,
        acceptanceCriteria: criteria,
        routedTo: templateTask.routeTo,
        dependencies,
      }
    })

    return {
      id: objectiveId,
      description: replaceVariables(templateObj.description, variables),
      status: 'pending' as const,
      tasks,
    }
  })

  // Generate mission description
  const descriptionParts = [
    template.name,
    ...Object.entries(variables)
      .filter(
        ([key]) => key.includes('NAME') || key.includes('DESCRIPTION') || key.includes('TARGET')
      )
      .map(([_, value]) => value),
  ]

  return {
    description: descriptionParts.join(': '),
    complexity: complexity ?? template.defaultComplexity,
    councilMode: councilMode ?? template.suggestedCouncilMode,
    objectives,
  }
}

/**
 * Validate that all required variables are provided
 */
export function validateVariables(
  template: MissionTemplate,
  variables: Record<string, string>
): { valid: boolean; missing: string[] } {
  const missing: string[] = []

  for (const variable of template.variables) {
    if (variable.required && !variables[variable.name]) {
      missing.push(variable.name)
    }
  }

  return {
    valid: missing.length === 0,
    missing,
  }
}

/**
 * Get variable prompts for a template
 */
export function getVariablePrompts(template: MissionTemplate): Array<{
  name: string
  prompt: string
  required: boolean
  example?: string
}> {
  return template.variables.map((v) => ({
    name: v.name,
    prompt: v.description,
    required: v.required,
    example: v.example,
  }))
}

// =============================================================================
// Template Summary
// =============================================================================

/**
 * Get a summary of a template
 */
export function getTemplateSummary(template: MissionTemplate): string {
  const lines: string[] = [
    `Template: ${template.name}`,
    `Type: ${template.type}`,
    `Complexity: ${template.defaultComplexity}`,
    `Council Mode: ${template.suggestedCouncilMode}`,
    '',
    template.description,
    '',
    `Objectives: ${template.objectives.length}`,
  ]

  for (let i = 0; i < template.objectives.length; i++) {
    const obj = template.objectives[i]
    lines.push(`  ${i + 1}. ${obj.description} (${obj.tasks.length} tasks)`)
  }

  if (template.variables.length > 0) {
    lines.push('')
    lines.push('Required Variables:')
    for (const v of template.variables.filter((v) => v.required)) {
      lines.push(`  - ${v.name}: ${v.description}`)
    }
  }

  return lines.join('\n')
}
