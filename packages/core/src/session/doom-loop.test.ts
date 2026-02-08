/**
 * Doom Loop Detection Tests
 *
 * Tests for DoomLoopDetector: consecutive call detection, history management,
 * tool reset, configuration, and global convenience functions.
 */

import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import {
  checkDoomLoop,
  clearDoomLoopHistory,
  DoomLoopDetector,
  getDoomLoopDetector,
  resetDoomLoopDetector,
} from './doom-loop.js'

// ============================================================================
// DoomLoopDetector Class
// ============================================================================

describe('DoomLoopDetector', () => {
  let detector: DoomLoopDetector

  beforeEach(() => {
    detector = new DoomLoopDetector({ threshold: 3, historySize: 10, autoBlock: false })
  })

  // ==========================================================================
  // Basic Detection
  // ==========================================================================

  describe('check', () => {
    it('does not detect loop on first call', () => {
      const result = detector.check('s1', 'read_file', { path: '/main.ts' })

      expect(result.detected).toBe(false)
      expect(result.consecutiveCount).toBe(1)
    })

    it('does not detect loop below threshold', () => {
      detector.check('s1', 'read_file', { path: '/main.ts' })
      const result = detector.check('s1', 'read_file', { path: '/main.ts' })

      expect(result.detected).toBe(false)
      expect(result.consecutiveCount).toBe(2)
    })

    it('detects loop at threshold', () => {
      detector.check('s1', 'read_file', { path: '/main.ts' })
      detector.check('s1', 'read_file', { path: '/main.ts' })
      const result = detector.check('s1', 'read_file', { path: '/main.ts' })

      expect(result.detected).toBe(true)
      expect(result.consecutiveCount).toBe(3)
      expect(result.repeatedCall).toEqual({ tool: 'read_file', params: { path: '/main.ts' } })
      expect(result.suggestion).toContain('read_file')
    })

    it('resets count when different tool is called', () => {
      detector.check('s1', 'read_file', { path: '/main.ts' })
      detector.check('s1', 'read_file', { path: '/main.ts' })
      detector.check('s1', 'write_file', { path: '/main.ts', content: 'new' })
      const result = detector.check('s1', 'read_file', { path: '/main.ts' })

      expect(result.detected).toBe(false)
      expect(result.consecutiveCount).toBe(1)
    })

    it('resets count when same tool called with different params', () => {
      detector.check('s1', 'read_file', { path: '/main.ts' })
      detector.check('s1', 'read_file', { path: '/main.ts' })
      detector.check('s1', 'read_file', { path: '/other.ts' }) // different path
      const result = detector.check('s1', 'read_file', { path: '/main.ts' })

      expect(result.detected).toBe(false)
      expect(result.consecutiveCount).toBe(1)
    })

    it('tracks sessions independently', () => {
      detector.check('s1', 'read_file', { path: '/main.ts' })
      detector.check('s1', 'read_file', { path: '/main.ts' })

      // Different session — should start fresh
      const result = detector.check('s2', 'read_file', { path: '/main.ts' })
      expect(result.detected).toBe(false)
      expect(result.consecutiveCount).toBe(1)
    })

    it('normalizes param key order for comparison', () => {
      detector.check('s1', 'bash', { cmd: 'ls', cwd: '/home' })
      detector.check('s1', 'bash', { cwd: '/home', cmd: 'ls' }) // same params, different order
      const result = detector.check('s1', 'bash', { cmd: 'ls', cwd: '/home' })

      expect(result.detected).toBe(true)
      expect(result.consecutiveCount).toBe(3)
    })
  })

  // ==========================================================================
  // History Management
  // ==========================================================================

  describe('history', () => {
    it('trims history to maxSize', () => {
      const smallDetector = new DoomLoopDetector({ threshold: 3, historySize: 3, autoBlock: false })

      smallDetector.check('s1', 'tool1', { a: 1 })
      smallDetector.check('s1', 'tool2', { a: 2 })
      smallDetector.check('s1', 'tool3', { a: 3 })
      smallDetector.check('s1', 'tool4', { a: 4 }) // pushes out tool1

      const history = smallDetector.getHistory('s1')
      expect(history).toHaveLength(3)
      expect(history[0]!.tool).toBe('tool2')
    })

    it('getHistory returns copy', () => {
      detector.check('s1', 'read_file', { path: '/main.ts' })

      const history1 = detector.getHistory('s1')
      const history2 = detector.getHistory('s1')

      expect(history1).not.toBe(history2)
      expect(history1).toEqual(history2)
    })

    it('getHistory returns empty for unknown session', () => {
      expect(detector.getHistory('unknown')).toEqual([])
    })

    it('history entries have correct structure', () => {
      detector.check('s1', 'read_file', { path: '/main.ts' })

      const history = detector.getHistory('s1')
      expect(history[0]!.tool).toBe('read_file')
      expect(history[0]!.paramsHash).toBeDefined()
      expect(history[0]!.timestamp).toBeGreaterThan(0)
    })
  })

  // ==========================================================================
  // Clear
  // ==========================================================================

  describe('clear', () => {
    it('clears history for specific session', () => {
      detector.check('s1', 'read_file', { path: '/a' })
      detector.check('s2', 'read_file', { path: '/b' })

      detector.clear('s1')

      expect(detector.getHistory('s1')).toEqual([])
      expect(detector.getHistory('s2')).toHaveLength(1)
    })

    it('clears all sessions when no id provided', () => {
      detector.check('s1', 'read_file', { path: '/a' })
      detector.check('s2', 'read_file', { path: '/b' })

      detector.clear()

      expect(detector.getHistory('s1')).toEqual([])
      expect(detector.getHistory('s2')).toEqual([])
    })
  })

  // ==========================================================================
  // Reset Tool
  // ==========================================================================

  describe('resetTool', () => {
    it('removes all entries for a specific tool', () => {
      detector.check('s1', 'read_file', { path: '/a' })
      detector.check('s1', 'read_file', { path: '/b' })
      detector.check('s1', 'write_file', { path: '/c', content: 'x' })

      detector.resetTool('s1', 'read_file')

      const history = detector.getHistory('s1')
      expect(history).toHaveLength(1)
      expect(history[0]!.tool).toBe('write_file')
    })

    it('does nothing for non-existent session', () => {
      // Should not throw
      detector.resetTool('unknown', 'read_file')
    })

    it('breaks doom loop after reset', () => {
      detector.check('s1', 'read_file', { path: '/main.ts' })
      detector.check('s1', 'read_file', { path: '/main.ts' })

      detector.resetTool('s1', 'read_file')

      // After reset, the next call starts fresh
      const result = detector.check('s1', 'read_file', { path: '/main.ts' })
      expect(result.detected).toBe(false)
      expect(result.consecutiveCount).toBe(1)
    })
  })

  // ==========================================================================
  // Configuration
  // ==========================================================================

  describe('configure', () => {
    it('updates threshold', () => {
      detector.configure({ threshold: 5 })

      for (let i = 0; i < 4; i++) {
        const result = detector.check('s1', 'read_file', { path: '/main.ts' })
        expect(result.detected).toBe(false)
      }

      const result = detector.check('s1', 'read_file', { path: '/main.ts' })
      expect(result.detected).toBe(true)
      expect(result.consecutiveCount).toBe(5)
    })

    it('getConfig returns current configuration', () => {
      const config = detector.getConfig()

      expect(config.threshold).toBe(3)
      expect(config.historySize).toBe(10)
      expect(config.autoBlock).toBe(false)
    })

    it('getConfig returns copy', () => {
      const config1 = detector.getConfig()
      const config2 = detector.getConfig()

      expect(config1).not.toBe(config2)
      expect(config1).toEqual(config2)
    })
  })

  // ==========================================================================
  // Continued Detection
  // ==========================================================================

  describe('continued detection', () => {
    it('keeps detecting after threshold exceeded', () => {
      for (let i = 0; i < 5; i++) {
        detector.check('s1', 'bash', { cmd: 'exit 1' })
      }

      const result = detector.check('s1', 'bash', { cmd: 'exit 1' })
      expect(result.detected).toBe(true)
      expect(result.consecutiveCount).toBe(6)
    })
  })
})

// ============================================================================
// Global Functions
// ============================================================================

describe('global doom loop functions', () => {
  beforeEach(() => {
    resetDoomLoopDetector()
  })

  afterEach(() => {
    resetDoomLoopDetector()
  })

  describe('getDoomLoopDetector', () => {
    it('returns same instance on repeated calls', () => {
      const a = getDoomLoopDetector()
      const b = getDoomLoopDetector()
      expect(a).toBe(b)
    })

    it('applies config on first call', () => {
      const detector = getDoomLoopDetector({ threshold: 7 })
      expect(detector.getConfig().threshold).toBe(7)
    })

    it('updates config on subsequent calls', () => {
      getDoomLoopDetector({ threshold: 3 })
      getDoomLoopDetector({ threshold: 10 })

      expect(getDoomLoopDetector().getConfig().threshold).toBe(10)
    })
  })

  describe('checkDoomLoop', () => {
    it('uses global detector', () => {
      const result = checkDoomLoop('s1', 'read_file', { path: '/main.ts' })
      expect(result.detected).toBe(false)
      expect(result.consecutiveCount).toBe(1)
    })

    it('detects loop via global function', () => {
      checkDoomLoop('s1', 'bash', { cmd: 'ls' })
      checkDoomLoop('s1', 'bash', { cmd: 'ls' })
      const result = checkDoomLoop('s1', 'bash', { cmd: 'ls' })

      expect(result.detected).toBe(true)
    })
  })

  describe('clearDoomLoopHistory', () => {
    it('clears specific session history', () => {
      checkDoomLoop('s1', 'read_file', { path: '/a' })
      clearDoomLoopHistory('s1')

      const history = getDoomLoopDetector().getHistory('s1')
      expect(history).toEqual([])
    })

    it('clears all history', () => {
      checkDoomLoop('s1', 'read_file', { path: '/a' })
      checkDoomLoop('s2', 'read_file', { path: '/b' })

      clearDoomLoopHistory()

      expect(getDoomLoopDetector().getHistory('s1')).toEqual([])
      expect(getDoomLoopDetector().getHistory('s2')).toEqual([])
    })
  })

  describe('resetDoomLoopDetector', () => {
    it('creates fresh instance on next access', () => {
      const first = getDoomLoopDetector({ threshold: 99 })
      resetDoomLoopDetector()
      const second = getDoomLoopDetector()

      expect(first).not.toBe(second)
      expect(second.getConfig().threshold).toBe(3) // back to default
    })
  })
})
