/**
 * Tests for Delta9 Learning Insights
 */

import { describe, it, expect, beforeEach } from 'vitest'
import {
  InsightGenerator,
  getInsightGenerator,
  resetInsightGenerator,
  generateCoordinatorInsights,
  generateWorkerInsights,
  formatInsightsForPrompt,
} from '../../src/learning/insights.js'
import { LearningEngine, resetLearningEngine } from '../../src/learning/engine.js'
import { resetEventStore } from '../../src/events/store.js'
import { existsSync, rmSync, mkdirSync } from 'node:fs'

describe('InsightGenerator', () => {
  const testDir = '/tmp/delta9-insights-test'
  let engine: LearningEngine
  let generator: InsightGenerator

  beforeEach(() => {
    // Clean up test directory
    if (existsSync(testDir)) {
      rmSync(testDir, { recursive: true })
    }
    mkdirSync(testDir, { recursive: true })

    resetEventStore()
    resetLearningEngine()
    resetInsightGenerator()

    engine = new LearningEngine({}, testDir)
    generator = new InsightGenerator(engine)
  })

  describe('Coordinator Insights', () => {
    it('should generate strategy insights', () => {
      // Record enough outcomes to generate strategy insights
      for (let i = 0; i < 5; i++) {
        engine.recordOutcome({
          taskId: `task-${i}`,
          success: i < 4, // 80% success
          agent: 'operator',
          duration: 1000,
          filesChanged: [],
          patternsApplied: [],
          strategy: 'file-based',
        })
      }

      const insights = generator.generateCoordinatorInsights({})
      const strategyInsights = insights.filter((i) => i.type === 'strategy')

      expect(strategyInsights.length).toBeGreaterThan(0)
      expect(strategyInsights[0].text).toContain('file-based')
      expect(strategyInsights[0].text).toContain('80%')
    })

    it('should generate anti-pattern warnings', () => {
      // Create anti-pattern
      const pattern = engine.learnPattern('Bad practice', 'file', 'Causes issues', 'failure', {
        relatedFiles: ['src/broken.ts'],
      })
      engine.markAsAntiPattern(pattern.id)

      // Apply failures
      for (let i = 0; i < 5; i++) {
        engine.applyPattern(pattern.id, false)
      }

      const insights = generator.generateCoordinatorInsights({
        files: ['src/broken.ts'],
      })
      const warnings = insights.filter((i) => i.type === 'warning')

      expect(warnings.length).toBeGreaterThan(0)
      expect(warnings[0].text).toContain('WARNING')
    })

    it('should generate pattern recommendations', () => {
      // Create high-confidence patterns
      engine.learnPattern('Good practice 1', 'general', 'Works well', 'success', {
        confidence: 0.9,
      })
      engine.learnPattern('Good practice 2', 'general', 'Also works', 'success', {
        confidence: 0.8,
      })

      const insights = generator.generateCoordinatorInsights({})
      const patternInsights = insights.filter((i) => i.type === 'pattern')

      expect(patternInsights.length).toBeGreaterThan(0)
      expect(patternInsights[0].text).toContain('Recommended')
    })

    it('should respect token budget', () => {
      // Create many patterns to exceed budget
      for (let i = 0; i < 20; i++) {
        engine.learnPattern(`Pattern ${i}`, 'general', 'Test', 'success', {
          confidence: 0.9,
        })
      }

      const smallBudgetGenerator = new InsightGenerator(engine, {
        coordinator: 100, // Very small budget
        worker: 100,
        maxPerCategory: 2,
      })

      const insights = smallBudgetGenerator.generateCoordinatorInsights({})
      expect(insights.length).toBeLessThanOrEqual(6) // 2 per category max
    })

    it('should sort by relevance', () => {
      engine.learnPattern('Low confidence', 'general', 'Test', 'user', { confidence: 0.3 })
      engine.learnPattern('High confidence', 'general', 'Test', 'success', { confidence: 0.95 })
      engine.learnPattern('Medium confidence', 'general', 'Test', 'user', { confidence: 0.6 })

      const insights = generator.generateCoordinatorInsights({})
      const patternInsights = insights.filter((i) => i.type === 'pattern')

      if (patternInsights.length >= 2) {
        expect(patternInsights[0].relevance).toBeGreaterThanOrEqual(patternInsights[1].relevance)
      }
    })
  })

  describe('Worker Insights', () => {
    it('should generate file-specific insights', () => {
      engine.learnPattern('Auth gotcha', 'file', 'Watch for race conditions', 'failure', {
        relatedFiles: ['src/auth.ts'],
        confidence: 0.8,
      })

      const insights = generator.generateWorkerInsights({
        files: ['src/auth.ts'],
        agent: 'operator',
      })
      const fileInsights = insights.filter((i) => i.type === 'file')

      expect(fileInsights.length).toBeGreaterThan(0)
      expect(fileInsights[0].text).toContain('Auth gotcha')
    })

    it('should generate agent tips', () => {
      // Record enough outcomes for the agent
      for (let i = 0; i < 6; i++) {
        engine.recordOutcome({
          taskId: `task-${i}`,
          success: i < 5, // ~83% success
          agent: 'operator',
          duration: 1000,
          filesChanged: [],
          patternsApplied: [],
        })
      }

      const insights = generator.generateWorkerInsights({
        files: ['test.ts'],
        agent: 'operator',
      })
      const tips = insights.filter((i) => i.type === 'tip')

      expect(tips.length).toBeGreaterThan(0)
      expect(tips[0].text).toContain('operator')
    })

    it('should include agent-specific anti-pattern tips', () => {
      const pattern = engine.learnPattern('Operator mistake', 'agent', 'Common error', 'failure', {
        relatedAgents: ['operator'],
      })
      engine.markAsAntiPattern(pattern.id)

      const insights = generator.generateWorkerInsights({
        files: [],
        agent: 'operator',
      })
      const tips = insights.filter((i) => i.type === 'tip')

      const operatorTip = tips.find((t) => t.text.includes('Watch out'))
      expect(operatorTip).toBeDefined()
    })
  })

  describe('Format for Prompt', () => {
    it('should format insights as markdown sections', () => {
      engine.learnPattern('Test pattern', 'general', 'Test', 'success', { confidence: 0.9 })

      const pattern = engine.learnPattern('Bad pattern', 'file', 'Fails', 'failure', {
        relatedFiles: ['test.ts'],
      })
      engine.markAsAntiPattern(pattern.id)
      for (let i = 0; i < 5; i++) {
        engine.applyPattern(pattern.id, false)
      }

      const insights = generator.generateCoordinatorInsights({ files: ['test.ts'] })
      const formatted = generator.formatForPrompt(insights)

      expect(formatted).toContain('**')
      expect(formatted).toContain('- ')
    })

    it('should group by type', () => {
      engine.learnPattern('Pattern 1', 'general', 'Test', 'success', { confidence: 0.9 })
      engine.learnPattern('Pattern 2', 'general', 'Test', 'success', { confidence: 0.8 })

      const insights = generator.generateCoordinatorInsights({})
      const formatted = generator.formatForPrompt(insights)

      expect(formatted).toContain('Recommended patterns')
    })

    it('should return empty string for no insights', () => {
      const formatted = generator.formatForPrompt([])
      expect(formatted).toBe('')
    })

    it('should show warnings first', () => {
      const pattern = engine.learnPattern('Danger', 'file', 'Bad', 'failure', {
        relatedFiles: ['test.ts'],
      })
      engine.markAsAntiPattern(pattern.id)
      for (let i = 0; i < 5; i++) {
        engine.applyPattern(pattern.id, false)
      }
      engine.learnPattern('Good', 'general', 'Good', 'success', { confidence: 0.9 })

      const insights = generator.generateCoordinatorInsights({ files: ['test.ts'] })
      const formatted = generator.formatForPrompt(insights)

      if (formatted.includes('Warnings') && formatted.includes('Recommended')) {
        const warningIndex = formatted.indexOf('Warnings')
        const patternIndex = formatted.indexOf('Recommended')
        expect(warningIndex).toBeLessThan(patternIndex)
      }
    })
  })

  describe('Convenience Functions', () => {
    it('should get default generator', () => {
      resetInsightGenerator()
      const gen1 = getInsightGenerator()
      const gen2 = getInsightGenerator()
      expect(gen1).toBe(gen2)
    })

    it('generateCoordinatorInsights should use default generator', () => {
      resetInsightGenerator()
      const insights = generateCoordinatorInsights({})
      expect(Array.isArray(insights)).toBe(true)
    })

    it('generateWorkerInsights should use default generator', () => {
      resetInsightGenerator()
      const insights = generateWorkerInsights({ files: ['test.ts'] })
      expect(Array.isArray(insights)).toBe(true)
    })

    it('formatInsightsForPrompt should use default generator', () => {
      resetInsightGenerator()
      const formatted = formatInsightsForPrompt([
        { type: 'tip', text: 'Test tip', relevance: 0.5 },
      ])
      expect(formatted).toContain('Tips')
    })
  })
})
