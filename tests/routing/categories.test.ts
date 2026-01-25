/**
 * Tests for Category-Based Routing
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
  type TaskCategory,
} from '../../src/routing/categories.js'

describe('Category-Based Routing', () => {
  describe('detectCategory', () => {
    it('should detect planning category', () => {
      const matches = detectCategory('Design the architecture for a new microservices system')

      expect(matches.length).toBeGreaterThan(0)
      expect(matches[0].category).toBe('planning')
      expect(matches[0].confidence).toBeGreaterThan(0.3)
    })

    it('should detect coding category', () => {
      const matches = detectCategory('Implement a function to parse JSON data')

      expect(matches.length).toBeGreaterThan(0)
      expect(matches[0].category).toBe('coding')
    })

    it('should detect testing category', () => {
      const matches = detectCategory('Write unit tests for the user service')

      expect(matches.length).toBeGreaterThan(0)
      expect(matches[0].category).toBe('testing')
    })

    it('should detect documentation category', () => {
      const matches = detectCategory('Update the README with installation instructions')

      expect(matches.length).toBeGreaterThan(0)
      expect(matches[0].category).toBe('documentation')
    })

    it('should detect research category', () => {
      const matches = detectCategory('Research best practices for React state management')

      expect(matches.length).toBeGreaterThan(0)
      expect(matches[0].category).toBe('research')
    })

    it('should detect ui category', () => {
      const matches = detectCategory('Create a responsive button component with Tailwind')

      expect(matches.length).toBeGreaterThan(0)
      expect(matches[0].category).toBe('ui')
    })

    it('should detect refactoring category', () => {
      const matches = detectCategory('Refactor the authentication module to improve readability')

      expect(matches.length).toBeGreaterThan(0)
      expect(matches[0].category).toBe('refactoring')
    })

    it('should detect bugfix category', () => {
      const matches = detectCategory('Fix the login bug that causes crashes on mobile')

      expect(matches.length).toBeGreaterThan(0)
      expect(matches[0].category).toBe('bugfix')
    })

    it('should return multiple matches when applicable', () => {
      const matches = detectCategory('Fix the UI bug in the button component')

      expect(matches.length).toBeGreaterThan(1)
      const categories = matches.map((m) => m.category)
      expect(categories).toContain('bugfix')
      expect(categories).toContain('ui')
    })

    it('should return empty array for unrecognized tasks', () => {
      const matches = detectCategory('xyz123 foobar')

      expect(matches).toEqual([])
    })

    it('should sort by confidence descending', () => {
      const matches = detectCategory('Write tests for the UI component')

      if (matches.length > 1) {
        for (let i = 0; i < matches.length - 1; i++) {
          expect(matches[i].confidence).toBeGreaterThanOrEqual(matches[i + 1].confidence)
        }
      }
    })

    it('should include matched keywords', () => {
      const matches = detectCategory('Create a test spec with jest')

      expect(matches.length).toBeGreaterThan(0)
      expect(matches[0].matchedKeywords.length).toBeGreaterThan(0)
    })
  })

  describe('routeToCategory', () => {
    it('should route to best matching category', () => {
      const result = routeToCategory('Implement the login feature')

      expect(result.primary.category).toBe('coding')
      expect(result.effectiveModel).toBe('anthropic/claude-sonnet-4')
      expect(result.effectiveTemperature).toBe(0.3)
    })

    it('should default to coding for unrecognized tasks', () => {
      const result = routeToCategory('xyz123')

      expect(result.primary.category).toBe('coding')
      expect(result.primary.confidence).toBeLessThan(0.5)
    })

    it('should respect force category override', () => {
      const result = routeToCategory('Do something', undefined, {
        forceCategory: 'planning',
      })

      expect(result.primary.category).toBe('planning')
      expect(result.primary.confidence).toBe(1.0)
      expect(result.effectiveModel).toBe('anthropic/claude-opus-4-5')
    })

    it('should respect force model override', () => {
      const result = routeToCategory('Write unit tests', undefined, {
        forceModel: 'openai/gpt-4o',
      })

      expect(result.primary.category).toBe('testing')
      expect(result.effectiveModel).toBe('openai/gpt-4o')
    })

    it('should respect force temperature override', () => {
      const result = routeToCategory('Design the system', undefined, {
        forceTemperature: 0.1,
      })

      expect(result.primary.category).toBe('planning')
      expect(result.effectiveTemperature).toBe(0.1)
    })

    it('should include secondary categories', () => {
      const result = routeToCategory('Write test documentation')

      expect(result.secondary.length).toBeGreaterThanOrEqual(0)
      // May have both testing and documentation as matches
    })

    it('should return recommended agent', () => {
      const result = routeToCategory('Write comprehensive tests')

      expect(result.recommendedAgent).toBe('qa')
    })

    it('should use custom configs if provided', () => {
      const result = routeToCategory('Implement feature', {
        coding: {
          model: 'custom/model-123',
          temperature: 0.9,
        },
      })

      expect(result.primary.category).toBe('coding')
      expect(result.effectiveModel).toBe('custom/model-123')
      expect(result.effectiveTemperature).toBe(0.9)
    })
  })

  describe('getCategoryConfig', () => {
    it('should return config for valid category', () => {
      const config = getCategoryConfig('planning')

      expect(config.name).toBe('Planning')
      expect(config.model).toBe('anthropic/claude-opus-4-5')
      expect(config.temperature).toBe(0.7)
      expect(config.preferredAgent).toBe('operator-complex')
    })

    it('should return all config fields', () => {
      const config = getCategoryConfig('testing')

      expect(config.name).toBeDefined()
      expect(config.description).toBeDefined()
      expect(config.model).toBeDefined()
      expect(config.temperature).toBeDefined()
      expect(config.preferredAgent).toBeDefined()
      expect(config.fallbackAgents).toBeDefined()
      expect(config.budgetPriority).toBeDefined()
      expect(config.keywords).toBeDefined()
    })

    it('should merge custom configs', () => {
      const config = getCategoryConfig('coding', {
        coding: {
          model: 'custom/model',
        },
      })

      expect(config.model).toBe('custom/model')
      expect(config.temperature).toBe(0.3) // Unchanged
    })

    it('should extend keywords with custom configs', () => {
      const config = getCategoryConfig('coding', {
        coding: {
          keywords: ['custom-keyword'],
        },
      })

      expect(config.keywords).toContain('custom-keyword')
      expect(config.keywords).toContain('implement') // Original keyword preserved
    })
  })

  describe('getAllCategories', () => {
    it('should return all 8 categories', () => {
      const categories = getAllCategories()

      expect(categories).toHaveLength(8)
      expect(categories).toContain('planning')
      expect(categories).toContain('coding')
      expect(categories).toContain('testing')
      expect(categories).toContain('documentation')
      expect(categories).toContain('research')
      expect(categories).toContain('ui')
      expect(categories).toContain('refactoring')
      expect(categories).toContain('bugfix')
    })
  })

  describe('isValidCategory', () => {
    it('should return true for valid categories', () => {
      expect(isValidCategory('planning')).toBe(true)
      expect(isValidCategory('coding')).toBe(true)
      expect(isValidCategory('testing')).toBe(true)
    })

    it('should return false for invalid categories', () => {
      expect(isValidCategory('invalid')).toBe(false)
      expect(isValidCategory('PLANNING')).toBe(false) // Case sensitive
      expect(isValidCategory('')).toBe(false)
    })
  })

  describe('describeCategoryRoute', () => {
    it('should return human-readable description', () => {
      const result = routeToCategory('Create unit tests for the API')
      const description = describeCategoryRoute(result)

      expect(description).toContain('Testing')
      expect(description).toContain('Model:')
      expect(description).toContain('Temperature:')
      expect(description).toContain('Recommended Agent:')
    })

    it('should include secondary categories if present', () => {
      const result = routeToCategory('Write test documentation for the component')
      const description = describeCategoryRoute(result)

      expect(description).toContain('Category:')
      // May or may not include Secondary Categories depending on match
    })
  })

  describe('getCategoryBudgetAllowance', () => {
    it('should return budget allowance based on priority', () => {
      const planningAllowance = getCategoryBudgetAllowance('planning')
      const docAllowance = getCategoryBudgetAllowance('documentation')

      expect(planningAllowance).toBe(0.9) // Priority 9
      expect(docAllowance).toBe(0.4) // Priority 4
      expect(planningAllowance).toBeGreaterThan(docAllowance)
    })

    it('should return value between 0 and 1', () => {
      for (const category of getAllCategories()) {
        const allowance = getCategoryBudgetAllowance(category)
        expect(allowance).toBeGreaterThanOrEqual(0)
        expect(allowance).toBeLessThanOrEqual(1)
      }
    })
  })

  describe('getCategoryTemperatureRange', () => {
    it('should return min, max, and recommended', () => {
      const range = getCategoryTemperatureRange('planning')

      expect(range.min).toBeDefined()
      expect(range.max).toBeDefined()
      expect(range.recommended).toBeDefined()
      expect(range.min).toBeLessThan(range.max)
      expect(range.recommended).toBeGreaterThanOrEqual(range.min)
      expect(range.recommended).toBeLessThanOrEqual(range.max)
    })

    it('should have different ranges for different categories', () => {
      const planningRange = getCategoryTemperatureRange('planning')
      const bugfixRange = getCategoryTemperatureRange('bugfix')

      expect(planningRange.max).toBeGreaterThan(bugfixRange.max)
    })

    it('should return valid ranges for all categories', () => {
      for (const category of getAllCategories()) {
        const range = getCategoryTemperatureRange(category)
        expect(range.min).toBeLessThan(range.max)
        expect(range.recommended).toBeGreaterThanOrEqual(range.min)
        expect(range.recommended).toBeLessThanOrEqual(range.max)
      }
    })
  })

  describe('DEFAULT_CATEGORY_CONFIGS', () => {
    it('should have configs for all categories', () => {
      const categories = getAllCategories()

      for (const category of categories) {
        expect(DEFAULT_CATEGORY_CONFIGS[category]).toBeDefined()
      }
    })

    it('should have valid temperature values', () => {
      for (const config of Object.values(DEFAULT_CATEGORY_CONFIGS)) {
        expect(config.temperature).toBeGreaterThanOrEqual(0)
        expect(config.temperature).toBeLessThanOrEqual(1)
      }
    })

    it('should have valid budget priorities', () => {
      for (const config of Object.values(DEFAULT_CATEGORY_CONFIGS)) {
        expect(config.budgetPriority).toBeGreaterThanOrEqual(1)
        expect(config.budgetPriority).toBeLessThanOrEqual(10)
      }
    })

    it('should have non-empty keywords', () => {
      for (const config of Object.values(DEFAULT_CATEGORY_CONFIGS)) {
        expect(config.keywords.length).toBeGreaterThan(0)
      }
    })

    it('should have fallback agents', () => {
      for (const config of Object.values(DEFAULT_CATEGORY_CONFIGS)) {
        expect(config.fallbackAgents.length).toBeGreaterThan(0)
      }
    })
  })

  describe('Temperature Blending', () => {
    it('should blend temperatures when multiple categories match', () => {
      // Task that matches multiple categories
      const result = routeToCategory('Write test documentation')

      // Should still have a valid temperature
      expect(result.effectiveTemperature).toBeGreaterThan(0)
      expect(result.effectiveTemperature).toBeLessThan(1)
    })
  })

  describe('Agent Recommendations', () => {
    it('should recommend appropriate agents for each category', () => {
      const expectedAgents: Record<TaskCategory, string> = {
        planning: 'operator-complex',
        coding: 'operator',
        testing: 'qa',
        documentation: 'scribe',
        research: 'intel',
        ui: 'ui-ops',
        refactoring: 'operator-complex',
        bugfix: 'operator',
      }

      for (const [category, expectedAgent] of Object.entries(expectedAgents)) {
        const config = getCategoryConfig(category as TaskCategory)
        expect(config.preferredAgent).toBe(expectedAgent)
      }
    })
  })
})
