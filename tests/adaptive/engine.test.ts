/**
 * Adaptive Council Engine Tests
 */

import { describe, it, expect, beforeEach } from 'vitest'
import { AdaptiveCouncilEngine, type AdaptiveConfig, type TaskCategory } from '../../src/adaptive/index.js'

describe('AdaptiveCouncilEngine', () => {
  let engine: AdaptiveCouncilEngine
  const defaultConfig: AdaptiveConfig = {
    enabled: true,
    learningRate: 0.1,
    explorationRate: 0.2,
    decayRate: 0.01,
    minSamplesForAdaptation: 3,
    dynamicSelection: true,
    categoryKeywords: {},
  }

  beforeEach(() => {
    engine = new AdaptiveCouncilEngine(defaultConfig)
    // Register test oracles
    engine.registerOracles(['claude', 'gpt', 'gemini', 'deepseek'])
  })

  describe('category detection', () => {
    it('should detect architecture category', () => {
      const category = engine.detectCategory('Design the system architecture for user authentication')
      expect(category).toBe('architecture')
    })

    it('should detect algorithm category', () => {
      const category = engine.detectCategory('Optimize the sorting algorithm for better performance')
      expect(category).toBe('algorithm')
    })

    it('should detect ui_frontend category', () => {
      const category = engine.detectCategory('Create a responsive React component for the dashboard')
      expect(category).toBe('ui_frontend')
    })

    it('should detect api_backend category', () => {
      const category = engine.detectCategory('Implement REST API endpoint for user registration')
      expect(category).toBe('api_backend')
    })

    it('should detect security category', () => {
      // Use keywords that match security: auth, authentication, jwt, oauth, encryption
      const category = engine.detectCategory('Implement OAuth authentication for the login flow')
      expect(category).toBe('security')
    })

    it('should detect debugging category', () => {
      // "bug" and "fix" are debugging keywords
      const category = engine.detectCategory('Fix the bug')
      expect(category).toBe('debugging')
    })

    it('should default to general for ambiguous tasks', () => {
      // Use text that doesn't match any specific category
      const category = engine.detectCategory('Update the project')
      expect(category).toBe('general')
    })
  })

  describe('oracle selection', () => {
    it('should select oracles without prior data', () => {
      // selectOracles takes (category, count)
      const result = engine.selectOracles('architecture', 2)

      expect(result.selectedOracles).toHaveLength(2)
      result.selectedOracles.forEach(oracle => {
        expect(['claude', 'gpt', 'gemini', 'deepseek']).toContain(oracle)
      })
    })

    it('should respect maximum count', () => {
      const result = engine.selectOracles('api_backend', 2)

      expect(result.selectedOracles).toHaveLength(2)
    })

    it('should handle requesting more oracles than registered', () => {
      const smallEngine = new AdaptiveCouncilEngine(defaultConfig)
      smallEngine.registerOracles(['claude'])
      const result = smallEngine.selectOracles('api_backend', 3)

      expect(result.selectedOracles).toHaveLength(1)
    })

    it('should return reason for selection', () => {
      const result = engine.selectOracles('ui_frontend', 2)

      expect(result.reason).toBeDefined()
      expect(typeof result.reason).toBe('string')
    })

    it('should return weights for selected oracles', () => {
      const result = engine.selectOracles('architecture', 2)

      expect(result.weights).toBeDefined()
      // Each selected oracle should have a weight
      for (const oracleId of result.selectedOracles) {
        expect(result.weights[oracleId]).toBeDefined()
        expect(result.weights[oracleId]).toBeGreaterThan(0)
      }
    })
  })

  describe('performance updates', () => {
    it('should record performance updates', () => {
      engine.updatePerformance({
        oracleId: 'claude',
        category: 'architecture' as TaskCategory,
        wasSuccessful: true,
        confidence: 0.9,
        responseTime: 5000,
        matchedConsensus: true,
      })

      const summary = engine.getPerformanceSummary()
      expect(summary['claude']).toBeDefined()
      expect(summary['claude']['architecture'].successfulRecommendations).toBe(1)
    })

    it('should track multiple updates', () => {
      for (let i = 0; i < 5; i++) {
        engine.updatePerformance({
          oracleId: 'claude',
          category: 'api_backend' as TaskCategory,
          wasSuccessful: i < 4, // 4 successes, 1 failure
          confidence: 0.8,
          responseTime: 3000,
          matchedConsensus: true,
        })
      }

      const summary = engine.getPerformanceSummary()
      expect(summary['claude']['api_backend'].totalConsultations).toBe(5)
      expect(summary['claude']['api_backend'].successfulRecommendations).toBe(4)
    })

    it('should update rolling averages', () => {
      engine.updatePerformance({
        oracleId: 'gpt',
        category: 'algorithm' as TaskCategory,
        wasSuccessful: true,
        confidence: 0.95,
        responseTime: 4000,
        matchedConsensus: true,
      })

      engine.updatePerformance({
        oracleId: 'gpt',
        category: 'algorithm' as TaskCategory,
        wasSuccessful: true,
        confidence: 0.85,
        responseTime: 3000,
        matchedConsensus: false,
      })

      const summary = engine.getPerformanceSummary()
      expect(summary['gpt']['algorithm'].averageConfidence).toBeGreaterThan(0)
      expect(summary['gpt']['algorithm'].averageResponseTime).toBeGreaterThan(0)
    })
  })

  describe('best oracle selection', () => {
    it('should return best oracle for category', () => {
      // Add performance data for multiple oracles
      engine.updatePerformance({
        oracleId: 'claude',
        category: 'security' as TaskCategory,
        wasSuccessful: true,
        confidence: 0.95,
        responseTime: 3000,
        matchedConsensus: true,
      })

      engine.updatePerformance({
        oracleId: 'gpt',
        category: 'security' as TaskCategory,
        wasSuccessful: false,
        confidence: 0.6,
        responseTime: 5000,
        matchedConsensus: false,
      })

      const best = engine.getBestOracle('security')
      expect(best).not.toBeNull()
      expect(best?.oracleId).toBe('claude')
    })

    it('should handle no performance data', () => {
      const best = engine.getBestOracle('testing')
      // Should return one of the available oracles with neutral score
      expect(best).not.toBeNull()
      expect(['claude', 'gpt', 'gemini', 'deepseek']).toContain(best?.oracleId)
    })
  })

  describe('oracle specialties', () => {
    it('should identify oracle specialties', () => {
      // Add high performance in one category
      for (let i = 0; i < 10; i++) {
        engine.updatePerformance({
          oracleId: 'gemini',
          category: 'ui_frontend' as TaskCategory,
          wasSuccessful: true,
          confidence: 0.95,
          responseTime: 2000,
          matchedConsensus: true,
        })
      }

      const specialties = engine.getOracleSpecialties('gemini', 60)
      expect(specialties).toContain('ui_frontend')
    })
  })

  describe('persistence', () => {
    it('should export and import state', () => {
      engine.updatePerformance({
        oracleId: 'claude',
        category: 'architecture' as TaskCategory,
        wasSuccessful: true,
        confidence: 0.9,
        responseTime: 3000,
        matchedConsensus: true,
      })

      const exported = engine.exportState()
      expect(exported.performance.length).toBeGreaterThan(0)

      // Create new engine and import
      const newEngine = new AdaptiveCouncilEngine(defaultConfig)
      newEngine.registerOracles(['claude', 'gpt'])
      newEngine.importState(exported)

      const summary = newEngine.getPerformanceSummary()
      expect(summary['claude']).toBeDefined()
    })
  })

  describe('statistics', () => {
    it('should return performance summary', () => {
      engine.updatePerformance({
        oracleId: 'claude',
        category: 'architecture' as TaskCategory,
        wasSuccessful: true,
        confidence: 0.9,
        responseTime: 3000,
        matchedConsensus: true,
      })

      engine.updatePerformance({
        oracleId: 'claude',
        category: 'security' as TaskCategory,
        wasSuccessful: true,
        confidence: 0.85,
        responseTime: 4000,
        matchedConsensus: true,
      })

      const summary = engine.getPerformanceSummary()
      expect(summary['claude']).toBeDefined()
      expect(Object.keys(summary['claude']).length).toBeGreaterThanOrEqual(2)
    })
  })
})
