/**
 * Tests for Delta9 Mission Templates
 */

import { describe, it, expect } from 'vitest'
import {
  // Registry
  templateRegistry,
  getTemplate,
  listTemplates,
  findTemplatesByType,
  findTemplatesByTag,
  suggestTemplate,
  // Instantiation
  instantiateTemplate,
  validateVariables,
  getVariablePrompts,
  getTemplateSummary,
  // Individual templates
  featureTemplate,
  simpleFeatureTemplate,
  complexFeatureTemplate,
  bugfixTemplate,
  quickBugfixTemplate,
  criticalBugfixTemplate,
  securityBugfixTemplate,
  refactorTemplate,
  quickRefactorTemplate,
  largeRefactorTemplate,
  performanceRefactorTemplate,
  typeSafetyRefactorTemplate,
  type MissionTemplate,
} from '../../src/templates/index.js'

describe('Mission Templates', () => {
  // ===========================================================================
  // Template Registry
  // ===========================================================================

  describe('Template Registry', () => {
    it('should have all expected templates', () => {
      const templateIds = Object.keys(templateRegistry)

      expect(templateIds).toContain('feature')
      expect(templateIds).toContain('feature:simple')
      expect(templateIds).toContain('feature:complex')
      expect(templateIds).toContain('bugfix')
      expect(templateIds).toContain('bugfix:quick')
      expect(templateIds).toContain('bugfix:critical')
      expect(templateIds).toContain('bugfix:security')
      expect(templateIds).toContain('refactor')
      expect(templateIds).toContain('refactor:quick')
      expect(templateIds).toContain('refactor:large')
      expect(templateIds).toContain('refactor:performance')
      expect(templateIds).toContain('refactor:types')
    })

    it('should have 12 templates total', () => {
      expect(Object.keys(templateRegistry)).toHaveLength(12)
    })

    it('should get template by ID', () => {
      expect(getTemplate('feature')).toBe(featureTemplate)
      expect(getTemplate('bugfix:quick')).toBe(quickBugfixTemplate)
      expect(getTemplate('nonexistent')).toBeUndefined()
    })

    it('should list all templates with metadata', () => {
      const list = listTemplates()

      expect(list.length).toBe(12)
      expect(list[0]).toHaveProperty('id')
      expect(list[0]).toHaveProperty('name')
      expect(list[0]).toHaveProperty('type')
      expect(list[0]).toHaveProperty('description')
      expect(list[0]).toHaveProperty('complexity')
      expect(list[0]).toHaveProperty('tags')
    })

    it('should find templates by type', () => {
      const featureTemplates = findTemplatesByType('feature')
      const bugfixTemplates = findTemplatesByType('bugfix')
      const refactorTemplates = findTemplatesByType('refactor')

      expect(featureTemplates).toHaveLength(3)
      expect(bugfixTemplates).toHaveLength(4)
      expect(refactorTemplates).toHaveLength(5)
    })

    it('should find templates by tag', () => {
      const quickTemplates = findTemplatesByTag('quick')
      const criticalTemplates = findTemplatesByTag('critical')

      expect(quickTemplates.length).toBeGreaterThan(0)
      expect(criticalTemplates.length).toBeGreaterThan(0)
    })
  })

  // ===========================================================================
  // Template Structure
  // ===========================================================================

  describe('Template Structure', () => {
    const allTemplates = Object.values(templateRegistry)

    it.each(allTemplates.map(t => [t.name, t]))('template "%s" should have valid structure', (_, template) => {
      expect(template.name).toBeDefined()
      expect(template.description).toBeDefined()
      expect(template.type).toMatch(/^(feature|bugfix|refactor|migration|documentation|testing|custom)$/)
      expect(template.defaultComplexity).toMatch(/^(low|medium|high|critical)$/)
      expect(template.suggestedCouncilMode).toMatch(/^(none|quick|standard|xhigh)$/)
      expect(Array.isArray(template.objectives)).toBe(true)
      expect(Array.isArray(template.variables)).toBe(true)
      expect(Array.isArray(template.tags)).toBe(true)
    })

    it.each(allTemplates.map(t => [t.name, t]))('template "%s" should have at least one objective', (_, template) => {
      expect(template.objectives.length).toBeGreaterThan(0)
    })

    it.each(allTemplates.map(t => [t.name, t]))('template "%s" objectives should have tasks', (_, template) => {
      for (const objective of template.objectives) {
        expect(objective.description).toBeDefined()
        expect(Array.isArray(objective.tasks)).toBe(true)
        expect(objective.tasks.length).toBeGreaterThan(0)
      }
    })

    it.each(allTemplates.map(t => [t.name, t]))('template "%s" tasks should have acceptance criteria', (_, template) => {
      for (const objective of template.objectives) {
        for (const task of objective.tasks) {
          expect(task.description).toBeDefined()
          expect(Array.isArray(task.acceptanceCriteria)).toBe(true)
          expect(task.acceptanceCriteria.length).toBeGreaterThan(0)
        }
      }
    })
  })

  // ===========================================================================
  // Feature Templates
  // ===========================================================================

  describe('Feature Templates', () => {
    it('featureTemplate should have standard complexity', () => {
      expect(featureTemplate.type).toBe('feature')
      expect(featureTemplate.defaultComplexity).toBe('medium')
      expect(featureTemplate.suggestedCouncilMode).toBe('standard')
    })

    it('simpleFeatureTemplate should be low complexity', () => {
      expect(simpleFeatureTemplate.type).toBe('feature')
      expect(simpleFeatureTemplate.defaultComplexity).toBe('low')
      expect(simpleFeatureTemplate.suggestedCouncilMode).toBe('quick')
    })

    it('complexFeatureTemplate should be critical complexity', () => {
      expect(complexFeatureTemplate.type).toBe('feature')
      expect(complexFeatureTemplate.defaultComplexity).toBe('critical')
      expect(complexFeatureTemplate.suggestedCouncilMode).toBe('xhigh')
    })

    it('featureTemplate should have required FEATURE_NAME variable', () => {
      const featureNameVar = featureTemplate.variables.find(v => v.name === '{{FEATURE_NAME}}')
      expect(featureNameVar).toBeDefined()
      expect(featureNameVar!.required).toBe(true)
    })
  })

  // ===========================================================================
  // Bugfix Templates
  // ===========================================================================

  describe('Bugfix Templates', () => {
    it('bugfixTemplate should have medium complexity', () => {
      expect(bugfixTemplate.type).toBe('bugfix')
      expect(bugfixTemplate.defaultComplexity).toBe('medium')
      expect(bugfixTemplate.suggestedCouncilMode).toBe('quick')
    })

    it('quickBugfixTemplate should use NONE council mode', () => {
      expect(quickBugfixTemplate.suggestedCouncilMode).toBe('none')
      expect(quickBugfixTemplate.defaultComplexity).toBe('low')
    })

    it('criticalBugfixTemplate should have additional severity variable', () => {
      const severityVar = criticalBugfixTemplate.variables.find(v => v.name === '{{SEVERITY}}')
      expect(severityVar).toBeDefined()
      expect(severityVar!.required).toBe(true)
    })

    it('securityBugfixTemplate should have CVE variable', () => {
      const cveVar = securityBugfixTemplate.variables.find(v => v.name === '{{CVE_ID}}')
      expect(cveVar).toBeDefined()
    })
  })

  // ===========================================================================
  // Refactor Templates
  // ===========================================================================

  describe('Refactor Templates', () => {
    it('refactorTemplate should have REFACTOR_TARGET variable', () => {
      const targetVar = refactorTemplate.variables.find(v => v.name === '{{REFACTOR_TARGET}}')
      expect(targetVar).toBeDefined()
      expect(targetVar!.required).toBe(true)
    })

    it('performanceRefactorTemplate should have performance-specific variables', () => {
      const perfTargetVar = performanceRefactorTemplate.variables.find(v => v.name === '{{PERFORMANCE_TARGET}}')
      const metricsVar = performanceRefactorTemplate.variables.find(v => v.name === '{{METRICS}}')

      expect(perfTargetVar).toBeDefined()
      expect(metricsVar).toBeDefined()
    })

    it('typeSafetyRefactorTemplate should focus on types', () => {
      expect(typeSafetyRefactorTemplate.tags).toContain('typescript')
      expect(typeSafetyRefactorTemplate.tags).toContain('types')
    })

    it('largeRefactorTemplate should require rollback plan', () => {
      const rollbackVar = largeRefactorTemplate.variables.find(v => v.name === '{{ROLLBACK_PLAN}}')
      expect(rollbackVar).toBeDefined()
      expect(rollbackVar!.required).toBe(true)
    })
  })

  // ===========================================================================
  // Template Suggestion
  // ===========================================================================

  describe('Template Suggestion', () => {
    it('should suggest bugfix template for bug-related descriptions', () => {
      const result = suggestTemplate('Fix the login bug')
      expect(result).not.toBeNull()
      expect(result!.template.type).toBe('bugfix')
    })

    it('should suggest feature template for feature-related descriptions', () => {
      const result = suggestTemplate('Add a new authentication feature')
      expect(result).not.toBeNull()
      expect(result!.template.type).toBe('feature')
    })

    it('should suggest refactor template for refactor-related descriptions', () => {
      const result = suggestTemplate('Refactor the user module')
      expect(result).not.toBeNull()
      expect(result!.template.type).toBe('refactor')
    })

    it('should suggest quick template for simple descriptions', () => {
      const result = suggestTemplate('Quick fix for typo')
      expect(result).not.toBeNull()
      expect(result!.templateId).toContain('quick')
    })

    it('should suggest critical template for critical descriptions', () => {
      const result = suggestTemplate('Critical security vulnerability fix')
      expect(result).not.toBeNull()
      expect(result!.templateId).toContain('security')
    })

    it('should suggest performance template for performance issues', () => {
      const result = suggestTemplate('Optimize slow database queries for better performance')
      expect(result).not.toBeNull()
      expect(result!.templateId).toContain('performance')
    })

    it('should return null for unrelated descriptions', () => {
      const result = suggestTemplate('zzzzqqqq nonsense words 9999')
      expect(result).toBeNull()
    })

    it('should return confidence score', () => {
      const result = suggestTemplate('Fix the critical production bug urgently')
      expect(result).not.toBeNull()
      expect(result!.confidence).toBeGreaterThan(0)
      expect(result!.confidence).toBeLessThanOrEqual(1)
    })
  })

  // ===========================================================================
  // Variable Validation
  // ===========================================================================

  describe('Variable Validation', () => {
    it('should validate required variables', () => {
      const result = validateVariables(featureTemplate, {})

      expect(result.valid).toBe(false)
      expect(result.missing).toContain('{{FEATURE_NAME}}')
      expect(result.missing).toContain('{{FEATURE_DESCRIPTION}}')
    })

    it('should pass validation with all required variables', () => {
      const result = validateVariables(featureTemplate, {
        '{{FEATURE_NAME}}': 'User Auth',
        '{{FEATURE_DESCRIPTION}}': 'Authentication system',
      })

      expect(result.valid).toBe(true)
      expect(result.missing).toHaveLength(0)
    })

    it('should get variable prompts', () => {
      const prompts = getVariablePrompts(featureTemplate)

      expect(prompts.length).toBeGreaterThan(0)
      expect(prompts[0]).toHaveProperty('name')
      expect(prompts[0]).toHaveProperty('prompt')
      expect(prompts[0]).toHaveProperty('required')
    })
  })

  // ===========================================================================
  // Template Instantiation
  // ===========================================================================

  describe('Template Instantiation', () => {
    it('should instantiate template with variables', () => {
      const result = instantiateTemplate(simpleFeatureTemplate, {
        variables: {
          '{{FEATURE_NAME}}': 'Dark Mode',
          '{{FEATURE_DESCRIPTION}}': 'Add dark mode support',
        },
      })

      expect(result.description).toContain('Dark Mode')
      expect(result.complexity).toBe('low')
      expect(result.councilMode).toBe('quick')
      expect(result.objectives.length).toBeGreaterThan(0)
    })

    it('should replace variables in task descriptions', () => {
      const result = instantiateTemplate(bugfixTemplate, {
        variables: {
          '{{BUG_DESCRIPTION}}': 'Login fails',
        },
      })

      const hasVariable = result.objectives.some(obj =>
        obj.description.includes('Login fails') ||
        obj.tasks.some(t => t.description.includes('Login fails'))
      )
      expect(hasVariable).toBe(true)
    })

    it('should override complexity', () => {
      const result = instantiateTemplate(featureTemplate, {
        variables: {
          '{{FEATURE_NAME}}': 'Test',
          '{{FEATURE_DESCRIPTION}}': 'Test feature',
        },
        complexity: 'high',
      })

      expect(result.complexity).toBe('high')
    })

    it('should override council mode', () => {
      const result = instantiateTemplate(featureTemplate, {
        variables: {
          '{{FEATURE_NAME}}': 'Test',
          '{{FEATURE_DESCRIPTION}}': 'Test feature',
        },
        councilMode: 'xhigh',
      })

      expect(result.councilMode).toBe('xhigh')
    })

    it('should add additional acceptance criteria', () => {
      const result = instantiateTemplate(quickBugfixTemplate, {
        variables: {
          '{{BUG_DESCRIPTION}}': 'Test bug',
        },
        additionalCriteria: ['Custom criterion 1', 'Custom criterion 2'],
      })

      // Check the last task of the last objective
      const lastObj = result.objectives[result.objectives.length - 1]
      const lastTask = lastObj.tasks[lastObj.tasks.length - 1]

      expect(lastTask.acceptanceCriteria).toContain('Custom criterion 1')
      expect(lastTask.acceptanceCriteria).toContain('Custom criterion 2')
    })

    it('should generate unique task IDs', () => {
      const result = instantiateTemplate(featureTemplate, {
        variables: {
          '{{FEATURE_NAME}}': 'Test',
          '{{FEATURE_DESCRIPTION}}': 'Test feature',
        },
      })

      const allTaskIds = result.objectives.flatMap(obj => obj.tasks.map(t => t.id))
      const uniqueIds = new Set(allTaskIds)

      expect(uniqueIds.size).toBe(allTaskIds.length)
    })

    it('should throw error for missing required variables', () => {
      expect(() => {
        instantiateTemplate(featureTemplate, {
          variables: {},
        })
      }).toThrow('Required variable')
    })

    it('should set task status to pending', () => {
      const result = instantiateTemplate(quickBugfixTemplate, {
        variables: {
          '{{BUG_DESCRIPTION}}': 'Test',
        },
      })

      for (const obj of result.objectives) {
        for (const task of obj.tasks) {
          expect(task.status).toBe('pending')
          expect(task.attempts).toBe(0)
        }
      }
    })

    it('should preserve routedTo from template', () => {
      const result = instantiateTemplate(bugfixTemplate, {
        variables: {
          '{{BUG_DESCRIPTION}}': 'Test bug',
        },
      })

      // Find a task with SURGEON routing
      let foundSurgeon = false
      for (const obj of result.objectives) {
        for (const task of obj.tasks) {
          if (task.routedTo === 'SURGEON') {
            foundSurgeon = true
          }
        }
      }
      expect(foundSurgeon).toBe(true)
    })
  })

  // ===========================================================================
  // Template Summary
  // ===========================================================================

  describe('Template Summary', () => {
    it('should generate readable summary', () => {
      const summary = getTemplateSummary(featureTemplate)

      expect(summary).toContain('Template: New Feature')
      expect(summary).toContain('Type: feature')
      expect(summary).toContain('Complexity: medium')
      expect(summary).toContain('Council Mode: standard')
      expect(summary).toContain('Objectives:')
      expect(summary).toContain('Required Variables:')
      expect(summary).toContain('{{FEATURE_NAME}}')
    })

    it('should list all objectives with task counts', () => {
      const summary = getTemplateSummary(featureTemplate)

      // Should show objective descriptions with task counts
      expect(summary).toMatch(/\d\. .+ \(\d+ tasks\)/)
    })
  })

  // ===========================================================================
  // Edge Cases
  // ===========================================================================

  describe('Edge Cases', () => {
    it('should handle empty variables object', () => {
      // quickRefactorTemplate has no strictly required variables... actually it does
      // Let's check quickBugfixTemplate
      expect(() => {
        validateVariables(quickBugfixTemplate, {})
      }).not.toThrow()
    })

    it('should handle extra variables', () => {
      const result = instantiateTemplate(quickBugfixTemplate, {
        variables: {
          '{{BUG_DESCRIPTION}}': 'Test',
          '{{EXTRA_VAR}}': 'Extra value',
        },
      })

      expect(result.objectives.length).toBeGreaterThan(0)
    })

    it('should handle variables with special regex characters', () => {
      const result = instantiateTemplate(quickBugfixTemplate, {
        variables: {
          '{{BUG_DESCRIPTION}}': 'Bug with $special [chars]',
        },
      })

      // Should not throw and should work
      expect(result.objectives.length).toBeGreaterThan(0)
    })
  })
})
