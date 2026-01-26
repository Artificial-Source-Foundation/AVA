/**
 * Tests for Delta9 Mission Templates
 *
 * Consolidated tests - verify key behaviors, not exhaustive property checks.
 */

import { describe, it, expect } from 'vitest'
import {
  templateRegistry,
  getTemplate,
  listTemplates,
  findTemplatesByType,
  suggestTemplate,
  instantiateTemplate,
  validateVariables,
  getVariablePrompts,
  featureTemplate,
  bugfixTemplate,
  refactorTemplate,
} from '../../src/templates/index.js'

describe('Template Registry', () => {
  it('should have all 12 templates with correct types', () => {
    const templates = Object.keys(templateRegistry)
    expect(templates).toHaveLength(12)

    // Check types
    expect(findTemplatesByType('feature')).toHaveLength(3)
    expect(findTemplatesByType('bugfix')).toHaveLength(4)
    expect(findTemplatesByType('refactor')).toHaveLength(5)
  })

  it('should get templates by ID', () => {
    expect(getTemplate('feature')).toBe(featureTemplate)
    expect(getTemplate('bugfix')).toBe(bugfixTemplate)
    expect(getTemplate('nonexistent')).toBeUndefined()
  })

  it('should list templates with metadata', () => {
    const list = listTemplates()
    expect(list).toHaveLength(12)
    expect(list[0]).toHaveProperty('id')
    expect(list[0]).toHaveProperty('name')
    expect(list[0]).toHaveProperty('type')
  })
})

describe('Template Structure', () => {
  it('all templates should have valid structure', () => {
    for (const template of Object.values(templateRegistry)) {
      expect(template.name).toBeDefined()
      expect(template.type).toMatch(/^(feature|bugfix|refactor|migration|documentation|testing|custom)$/)
      expect(template.objectives.length).toBeGreaterThan(0)

      // Each objective should have tasks with acceptance criteria
      for (const obj of template.objectives) {
        expect(obj.tasks.length).toBeGreaterThan(0)
        for (const task of obj.tasks) {
          expect(task.acceptanceCriteria.length).toBeGreaterThan(0)
        }
      }
    }
  })
})

describe('Template Types', () => {
  it('feature templates should have correct complexity', () => {
    expect(featureTemplate.defaultComplexity).toBe('medium')
    expect(getTemplate('feature:simple')?.defaultComplexity).toBe('low')
    expect(getTemplate('feature:complex')?.defaultComplexity).toBe('critical')
  })

  it('bugfix templates should have appropriate settings', () => {
    expect(bugfixTemplate.type).toBe('bugfix')
    expect(getTemplate('bugfix:quick')?.suggestedCouncilMode).toBe('none')
    expect(getTemplate('bugfix:security')?.variables.some(v => v.name === '{{CVE_ID}}')).toBe(true)
  })

  it('refactor templates should have required variables', () => {
    expect(refactorTemplate.variables.some(v => v.name === '{{REFACTOR_TARGET}}')).toBe(true)
    expect(getTemplate('refactor:large')?.variables.some(v => v.name === '{{ROLLBACK_PLAN}}')).toBe(true)
  })
})

describe('Template Suggestion', () => {
  it('should suggest appropriate templates by description', () => {
    expect(suggestTemplate('Fix the login bug')?.template.type).toBe('bugfix')
    expect(suggestTemplate('Add new feature')?.template.type).toBe('feature')
    expect(suggestTemplate('Refactor module')?.template.type).toBe('refactor')
    expect(suggestTemplate('Critical security fix')?.templateId).toContain('security')
    expect(suggestTemplate('zzzzqqqq nonsense')).toBeNull()
  })

  it('should return confidence scores', () => {
    const result = suggestTemplate('Fix the critical bug')
    expect(result?.confidence).toBeGreaterThan(0)
    expect(result?.confidence).toBeLessThanOrEqual(1)
  })
})

describe('Variable Validation', () => {
  it('should validate required variables', () => {
    const missing = validateVariables(featureTemplate, {})
    expect(missing.valid).toBe(false)
    expect(missing.missing).toContain('{{FEATURE_NAME}}')

    const valid = validateVariables(featureTemplate, {
      '{{FEATURE_NAME}}': 'Test',
      '{{FEATURE_DESCRIPTION}}': 'Desc',
    })
    expect(valid.valid).toBe(true)
  })

  it('should get variable prompts', () => {
    const prompts = getVariablePrompts(featureTemplate)
    expect(prompts.length).toBeGreaterThan(0)
    expect(prompts[0]).toHaveProperty('name')
    expect(prompts[0]).toHaveProperty('prompt')
  })
})

describe('Template Instantiation', () => {
  it('should instantiate template with variables', () => {
    const result = instantiateTemplate(featureTemplate, {
      variables: {
        '{{FEATURE_NAME}}': 'User Auth',
        '{{FEATURE_DESCRIPTION}}': 'Login system',
      },
    })

    expect(result.description).toContain('User Auth')
    expect(result.objectives.length).toBeGreaterThan(0)
  })

  it('should throw on missing required variables', () => {
    expect(() => instantiateTemplate(featureTemplate, { variables: {} })).toThrow()
  })
})
