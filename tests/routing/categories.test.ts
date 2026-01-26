/**
 * Tests for Category-Based Routing
 *
 * Uses representative sampling - tests 3 categories to verify the pattern works.
 */

import { describe, it, expect } from 'vitest'
import {
  detectCategory,
  routeToCategory,
  getCategoryConfig,
  getAllCategories,
  isValidCategory,
  describeCategoryRoute,
  getCategoryBudgetAllowance,
  getCategoryTemperatureRange,
  DEFAULT_CATEGORY_CONFIGS,
} from '../../src/routing/categories.js'

describe('detectCategory', () => {
  it('should detect categories from task descriptions', () => {
    // Test representative categories
    expect(detectCategory('Design the architecture')[0]?.category).toBe('planning')
    expect(detectCategory('Implement a function')[0]?.category).toBe('coding')
    expect(detectCategory('Write unit tests')[0]?.category).toBe('testing')
    expect(detectCategory('Fix the login bug')[0]?.category).toBe('bugfix')
  })

  it('should return multiple matches and sort by confidence', () => {
    const matches = detectCategory('Fix the UI bug in the button')
    expect(matches.length).toBeGreaterThan(1)

    // Check sorted by confidence
    for (let i = 0; i < matches.length - 1; i++) {
      expect(matches[i].confidence).toBeGreaterThanOrEqual(matches[i + 1].confidence)
    }
  })

  it('should return empty array for unrecognized tasks', () => {
    expect(detectCategory('xyz123 foobar')).toEqual([])
  })

  it('should include matched keywords in results', () => {
    const matches = detectCategory('Create a test spec with jest')
    expect(matches[0].matchedKeywords.length).toBeGreaterThan(0)
  })
})

describe('routeToCategory', () => {
  it('should route to best matching category with correct settings', () => {
    const result = routeToCategory('Implement the login feature')
    expect(result.primary.category).toBe('coding')
    expect(result.effectiveModel).toBe('anthropic/claude-sonnet-4-5')
    expect(result.recommendedAgent).toBe('operator')
  })

  it('should default to coding for unrecognized tasks', () => {
    const result = routeToCategory('xyz123')
    expect(result.primary.category).toBe('coding')
    expect(result.primary.confidence).toBeLessThan(0.5)
  })

  it('should respect override options', () => {
    // Force category
    expect(routeToCategory('xyz', undefined, { forceCategory: 'planning' }).primary.category).toBe('planning')
    // Force model
    expect(routeToCategory('Write tests', undefined, { forceModel: 'openai/gpt-4o' }).effectiveModel).toBe('openai/gpt-4o')
    // Force temperature
    expect(routeToCategory('Design system', undefined, { forceTemperature: 0.1 }).effectiveTemperature).toBe(0.1)
  })

  it('should use custom configs if provided', () => {
    const result = routeToCategory('Implement feature', {
      coding: { model: 'custom/model-123', temperature: 0.9 },
    })
    expect(result.effectiveModel).toBe('custom/model-123')
    expect(result.effectiveTemperature).toBe(0.9)
  })
})

describe('getCategoryConfig', () => {
  it('should return complete config for category', () => {
    const config = getCategoryConfig('planning')
    expect(config.name).toBe('Planning')
    expect(config.model).toBe('anthropic/claude-opus-4-5')
    expect(config.temperature).toBe(0.7)
    expect(config.preferredAgent).toBe('operator-complex')
    expect(config.keywords.length).toBeGreaterThan(0)
  })

  it('should merge and extend custom configs', () => {
    const config = getCategoryConfig('coding', {
      coding: { model: 'custom/model', keywords: ['custom-keyword'] },
    })
    expect(config.model).toBe('custom/model')
    expect(config.keywords).toContain('custom-keyword')
    expect(config.keywords).toContain('implement') // Original preserved
  })
})

describe('utility functions', () => {
  it('getAllCategories returns all 8 categories', () => {
    const categories = getAllCategories()
    expect(categories).toHaveLength(8)
    expect(categories).toContain('planning')
    expect(categories).toContain('bugfix')
  })

  it('isValidCategory validates correctly', () => {
    expect(isValidCategory('planning')).toBe(true)
    expect(isValidCategory('invalid')).toBe(false)
    expect(isValidCategory('PLANNING')).toBe(false) // Case sensitive
  })

  it('describeCategoryRoute returns readable description', () => {
    const result = routeToCategory('Create unit tests')
    const desc = describeCategoryRoute(result)
    expect(desc).toContain('Category:')
    expect(desc).toContain('Model:')
  })

  it('getCategoryBudgetAllowance returns valid allowances', () => {
    expect(getCategoryBudgetAllowance('planning')).toBe(0.9)
    expect(getCategoryBudgetAllowance('documentation')).toBe(0.4)
  })

  it('getCategoryTemperatureRange returns valid ranges', () => {
    const range = getCategoryTemperatureRange('planning')
    expect(range.min).toBeLessThan(range.max)
    expect(range.recommended).toBeGreaterThanOrEqual(range.min)
    expect(range.recommended).toBeLessThanOrEqual(range.max)
  })
})

describe('DEFAULT_CATEGORY_CONFIGS', () => {
  it('should have valid configs for all categories', () => {
    for (const config of Object.values(DEFAULT_CATEGORY_CONFIGS)) {
      expect(config.temperature).toBeGreaterThanOrEqual(0)
      expect(config.temperature).toBeLessThanOrEqual(1)
      expect(config.budgetPriority).toBeGreaterThanOrEqual(1)
      expect(config.budgetPriority).toBeLessThanOrEqual(10)
      expect(config.keywords.length).toBeGreaterThan(0)
      expect(config.fallbackAgents.length).toBeGreaterThan(0)
    }
  })
})
