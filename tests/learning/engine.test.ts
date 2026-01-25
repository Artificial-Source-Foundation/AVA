/**
 * Tests for Delta9 Learning Engine
 */

import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { LearningEngine, getLearningEngine, resetLearningEngine } from '../../src/learning/engine.js'
import { resetEventStore } from '../../src/events/store.js'
import { existsSync, unlinkSync, mkdirSync, rmSync } from 'node:fs'
import { join } from 'node:path'

describe('LearningEngine', () => {
  const testDir = '/tmp/delta9-learning-test'
  let engine: LearningEngine

  beforeEach(() => {
    // Clean up test directory
    if (existsSync(testDir)) {
      rmSync(testDir, { recursive: true })
    }
    mkdirSync(testDir, { recursive: true })

    resetEventStore()
    resetLearningEngine()
    engine = new LearningEngine({}, testDir)
  })

  afterEach(() => {
    if (existsSync(testDir)) {
      rmSync(testDir, { recursive: true })
    }
  })

  describe('Pattern Learning', () => {
    it('should learn a new pattern', () => {
      const pattern = engine.learnPattern(
        'Use async/await for file operations',
        'general',
        'Best practice for Node.js file handling',
        'user',
        { confidence: 0.8, tags: ['node', 'async'] }
      )

      expect(pattern.id).toBeDefined()
      expect(pattern.pattern).toBe('Use async/await for file operations')
      expect(pattern.category).toBe('general')
      expect(pattern.baseConfidence).toBe(0.8)
      expect(pattern.currentConfidence).toBe(0.8)
      expect(pattern.source).toBe('user')
      expect(pattern.tags).toContain('node')
      expect(pattern.isAntiPattern).toBe(false)
    })

    it('should update existing pattern with same name and category', () => {
      engine.learnPattern('Same pattern', 'file', 'Context 1', 'user', { confidence: 0.5 })
      const updated = engine.learnPattern('Same pattern', 'file', 'Context 2', 'success', {
        confidence: 0.9,
        relatedFiles: ['test.ts'],
      })

      expect(updated.baseConfidence).toBe(0.9) // Takes max
      expect(updated.relatedFiles).toContain('test.ts')

      // Should only have one pattern
      const patterns = engine.getPatternsByCategory('file')
      expect(patterns.length).toBe(1)
    })

    it('should get patterns by category', () => {
      engine.learnPattern('Strategy 1', 'strategy', 'Test', 'user')
      engine.learnPattern('Strategy 2', 'strategy', 'Test', 'user')
      engine.learnPattern('File pattern', 'file', 'Test', 'user')

      const strategies = engine.getPatternsByCategory('strategy')
      expect(strategies.length).toBe(2)

      const files = engine.getPatternsByCategory('file')
      expect(files.length).toBe(1)
    })

    it('should get patterns for specific files', () => {
      engine.learnPattern('Auth pattern', 'file', 'Auth gotcha', 'failure', {
        relatedFiles: ['src/auth.ts'],
      })
      engine.learnPattern('DB pattern', 'file', 'DB gotcha', 'failure', {
        relatedFiles: ['src/db.ts'],
      })

      const authPatterns = engine.getPatternsForFiles(['src/auth.ts'])
      expect(authPatterns.length).toBe(1)
      expect(authPatterns[0].pattern).toBe('Auth pattern')
    })

    it('should record pattern application', () => {
      const pattern = engine.learnPattern('Test pattern', 'general', 'Test', 'user')

      engine.applyPattern(pattern.id, true)
      engine.applyPattern(pattern.id, true)
      engine.applyPattern(pattern.id, false)

      const updated = engine.getPattern(pattern.id)
      expect(updated?.applications).toBe(3)
      expect(updated?.successes).toBe(2)
      expect(updated?.failures).toBe(1)
    })
  })

  describe('Anti-Pattern Detection', () => {
    it('should detect anti-pattern when failure rate exceeds threshold', () => {
      const pattern = engine.learnPattern('Bad practice', 'general', 'Test', 'user')

      // Apply with mostly failures (5 applications, 4 failures = 80% failure rate)
      engine.applyPattern(pattern.id, true)
      engine.applyPattern(pattern.id, false)
      engine.applyPattern(pattern.id, false)
      engine.applyPattern(pattern.id, false)
      engine.applyPattern(pattern.id, false)

      const updated = engine.getPattern(pattern.id)
      expect(updated?.isAntiPattern).toBe(true)
    })

    it('should not mark as anti-pattern below min applications', () => {
      const pattern = engine.learnPattern('New pattern', 'general', 'Test', 'user')

      // Only 3 applications (below min of 5)
      engine.applyPattern(pattern.id, false)
      engine.applyPattern(pattern.id, false)
      engine.applyPattern(pattern.id, false)

      const updated = engine.getPattern(pattern.id)
      expect(updated?.isAntiPattern).toBe(false)
    })

    it('should allow manual anti-pattern marking', () => {
      const pattern = engine.learnPattern('Known bad', 'general', 'Test', 'user')
      engine.markAsAntiPattern(pattern.id)

      const updated = engine.getPattern(pattern.id)
      expect(updated?.isAntiPattern).toBe(true)
    })

    it('should rehabilitate anti-pattern', () => {
      const pattern = engine.learnPattern('Rehabilitate me', 'general', 'Test', 'user')
      engine.markAsAntiPattern(pattern.id)
      expect(engine.getPattern(pattern.id)?.isAntiPattern).toBe(true)

      engine.rehabilitatePattern(pattern.id)
      const updated = engine.getPattern(pattern.id)
      expect(updated?.isAntiPattern).toBe(false)
      expect(updated?.applications).toBe(0) // Reset counters
    })

    it('should get all anti-patterns', () => {
      const p1 = engine.learnPattern('Good', 'general', 'Test', 'user')
      const p2 = engine.learnPattern('Bad', 'general', 'Test', 'user')
      const p3 = engine.learnPattern('Also bad', 'general', 'Test', 'user')

      engine.markAsAntiPattern(p2.id)
      engine.markAsAntiPattern(p3.id)

      const antiPatterns = engine.getAntiPatterns()
      expect(antiPatterns.length).toBe(2)
    })
  })

  describe('Outcome Tracking', () => {
    it('should record outcome', () => {
      const outcome = engine.recordOutcome({
        taskId: 'task-1',
        success: true,
        agent: 'operator',
        duration: 5000,
        filesChanged: ['src/test.ts'],
        patternsApplied: [],
        strategy: 'file-based',
      })

      expect(outcome.id).toBeDefined()
      expect(outcome.timestamp).toBeDefined()
      expect(outcome.success).toBe(true)
    })

    it('should update patterns when recording outcome', () => {
      const pattern = engine.learnPattern('Test pattern', 'general', 'Test', 'user')

      engine.recordOutcome({
        taskId: 'task-1',
        success: true,
        agent: 'operator',
        duration: 5000,
        filesChanged: [],
        patternsApplied: [pattern.id],
      })

      const updated = engine.getPattern(pattern.id)
      expect(updated?.successes).toBe(1)
    })

    it('should learn from failures', () => {
      engine.recordOutcome({
        taskId: 'task-1',
        success: false,
        agent: 'operator',
        duration: 5000,
        filesChanged: ['src/broken.ts'],
        patternsApplied: [],
        error: 'Something went wrong',
        errorCode: 'ERR_BROKEN',
      })

      // Should create file pattern
      const filePatterns = engine.getPatternsForFiles(['src/broken.ts'])
      expect(filePatterns.length).toBe(1)
    })

    it('should get recent outcomes', () => {
      for (let i = 0; i < 5; i++) {
        engine.recordOutcome({
          taskId: `task-${i}`,
          success: i % 2 === 0,
          agent: 'operator',
          duration: 1000,
          filesChanged: [],
          patternsApplied: [],
        })
      }

      const recent = engine.getRecentOutcomes(3)
      expect(recent.length).toBe(3)
      expect(recent[0].taskId).toBe('task-4') // Most recent first
    })

    it('should get outcomes for agent', () => {
      engine.recordOutcome({
        taskId: 'task-1',
        success: true,
        agent: 'operator',
        duration: 1000,
        filesChanged: [],
        patternsApplied: [],
      })
      engine.recordOutcome({
        taskId: 'task-2',
        success: true,
        agent: 'validator',
        duration: 1000,
        filesChanged: [],
        patternsApplied: [],
      })

      const operatorOutcomes = engine.getOutcomesForAgent('operator')
      expect(operatorOutcomes.length).toBe(1)
    })

    it('should calculate agent success rate', () => {
      engine.recordOutcome({
        taskId: 'task-1',
        success: true,
        agent: 'operator',
        duration: 1000,
        filesChanged: [],
        patternsApplied: [],
      })
      engine.recordOutcome({
        taskId: 'task-2',
        success: true,
        agent: 'operator',
        duration: 1000,
        filesChanged: [],
        patternsApplied: [],
      })
      engine.recordOutcome({
        taskId: 'task-3',
        success: false,
        agent: 'operator',
        duration: 1000,
        filesChanged: [],
        patternsApplied: [],
      })

      const rate = engine.getAgentSuccessRate('operator')
      expect(rate).toBeCloseTo(0.667, 2)
    })
  })

  describe('Strategy Tracking', () => {
    it('should track strategy success rates', () => {
      // Record multiple outcomes with strategies
      engine.recordOutcome({
        taskId: 'task-1',
        success: true,
        agent: 'operator',
        duration: 1000,
        filesChanged: [],
        patternsApplied: [],
        strategy: 'file-based',
      })
      engine.recordOutcome({
        taskId: 'task-2',
        success: true,
        agent: 'operator',
        duration: 1000,
        filesChanged: [],
        patternsApplied: [],
        strategy: 'file-based',
      })
      engine.recordOutcome({
        taskId: 'task-3',
        success: false,
        agent: 'operator',
        duration: 1000,
        filesChanged: [],
        patternsApplied: [],
        strategy: 'file-based',
      })

      const rates = engine.getStrategySuccessRates()
      const fileBasedRate = rates.find((r) => r.strategy === 'file-based')
      expect(fileBasedRate).toBeDefined()
      expect(fileBasedRate?.successRate).toBeCloseTo(0.667, 2)
      expect(fileBasedRate?.applications).toBe(3)
    })
  })

  describe('Confidence Decay', () => {
    it('should calculate decayed confidence', () => {
      const baseConfidence = 1.0
      const halfLife = 90 // days

      // 90 days ago = 50% decay
      const ninetyDaysAgo = new Date()
      ninetyDaysAgo.setDate(ninetyDaysAgo.getDate() - 90)

      const decayed = engine.calculateDecayedConfidence(baseConfidence, ninetyDaysAgo)
      expect(decayed).toBeCloseTo(0.5, 1)
    })

    it('should refresh pattern confidence', () => {
      const pattern = engine.learnPattern('Test', 'general', 'Test', 'user', { confidence: 0.5 })

      // Manually set old date
      const oldPattern = engine.getPattern(pattern.id)!
      const oldDate = new Date()
      oldDate.setDate(oldDate.getDate() - 45)

      engine.refreshPattern(pattern.id)
      const refreshed = engine.getPattern(pattern.id)

      // Confidence should be reset to base
      expect(refreshed?.currentConfidence).toBe(refreshed?.baseConfidence)
    })
  })

  describe('Statistics', () => {
    it('should track total patterns', () => {
      engine.learnPattern('P1', 'general', 'Test', 'user')
      engine.learnPattern('P2', 'file', 'Test', 'user')

      const stats = engine.getStats()
      expect(stats.totalPatterns).toBe(2)
    })

    it('should track total anti-patterns', () => {
      const p1 = engine.learnPattern('P1', 'general', 'Test', 'user')
      engine.learnPattern('P2', 'file', 'Test', 'user')
      engine.markAsAntiPattern(p1.id)

      const stats = engine.getStats()
      expect(stats.totalAntiPatterns).toBe(1)
    })

    it('should track overall success rate', () => {
      engine.recordOutcome({
        taskId: 'task-1',
        success: true,
        agent: 'operator',
        duration: 1000,
        filesChanged: [],
        patternsApplied: [],
      })
      engine.recordOutcome({
        taskId: 'task-2',
        success: false,
        agent: 'operator',
        duration: 1000,
        filesChanged: [],
        patternsApplied: [],
      })

      const stats = engine.getStats()
      expect(stats.successRate).toBe(0.5)
    })
  })

  describe('Singleton', () => {
    it('should return same instance', () => {
      resetLearningEngine()
      const engine1 = getLearningEngine()
      const engine2 = getLearningEngine()
      expect(engine1).toBe(engine2)
    })

    it('should reset singleton', () => {
      const engine1 = getLearningEngine()
      resetLearningEngine()
      const engine2 = getLearningEngine()
      expect(engine1).not.toBe(engine2)
    })
  })

  describe('Clear', () => {
    it('should clear all data', () => {
      engine.learnPattern('Test', 'general', 'Test', 'user')
      engine.recordOutcome({
        taskId: 'task-1',
        success: true,
        agent: 'operator',
        duration: 1000,
        filesChanged: [],
        patternsApplied: [],
      })

      engine.clear()

      const stats = engine.getStats()
      expect(stats.totalPatterns).toBe(0)
      expect(stats.totalOutcomes).toBe(0)
    })
  })
})
