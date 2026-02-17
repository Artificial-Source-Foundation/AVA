/**
 * Task Router Tests
 * Verifies keyword analysis and worker auto-selection
 */

import { describe, expect, it } from 'vitest'
import { createWorkerRegistry } from './registry.js'
import { analyzeTask, selectWorker } from './router.js'
import type { WorkerDefinition } from './types.js'

// ============================================================================
// Test Helpers
// ============================================================================

function createTestRegistry() {
  const registry = createWorkerRegistry()
  const workers: WorkerDefinition[] = [
    {
      name: 'coder',
      displayName: 'Coder',
      description: 'Writes code',
      systemPrompt: 'Code prompt',
      tools: ['read', 'write'],
    },
    {
      name: 'tester',
      displayName: 'Tester',
      description: 'Writes tests',
      systemPrompt: 'Test prompt',
      tools: ['read', 'write', 'bash'],
    },
    {
      name: 'reviewer',
      displayName: 'Reviewer',
      description: 'Reviews code',
      systemPrompt: 'Review prompt',
      tools: ['read'],
    },
    {
      name: 'researcher',
      displayName: 'Researcher',
      description: 'Researches information',
      systemPrompt: 'Research prompt',
      tools: ['read', 'grep'],
    },
    {
      name: 'debugger',
      displayName: 'Debugger',
      description: 'Debugs issues',
      systemPrompt: 'Debug prompt',
      tools: ['read', 'write', 'bash'],
    },
  ]
  registry.registerAll(workers)
  return registry
}

// ============================================================================
// Tests
// ============================================================================

describe('Task Router', () => {
  describe('analyzeTask', () => {
    it('identifies test tasks', () => {
      const analysis = analyzeTask('Write tests for the auth module')
      expect(analysis.taskType).toBe('test')
      expect(analysis.confidence).toBeGreaterThanOrEqual(0.5)
      expect(analysis.keywords).toContain('write test')
    })

    it('identifies test tasks with "add test"', () => {
      const analysis = analyzeTask('Add test coverage for utils.ts')
      expect(analysis.taskType).toBe('test')
    })

    it('identifies review tasks', () => {
      const analysis = analyzeTask('Review the changes in pull request')
      expect(analysis.taskType).toBe('review')
      expect(analysis.confidence).toBeGreaterThanOrEqual(0.5)
    })

    it('identifies audit tasks as review', () => {
      const analysis = analyzeTask('Audit the security of the auth flow')
      expect(analysis.taskType).toBe('review')
    })

    it('identifies research tasks', () => {
      const analysis = analyzeTask('Research how the caching layer works')
      expect(analysis.taskType).toBe('research')
      expect(analysis.confidence).toBeGreaterThanOrEqual(0.5)
    })

    it('identifies "find" tasks as research', () => {
      const analysis = analyzeTask('Find all usages of deprecated API')
      expect(analysis.taskType).toBe('research')
    })

    it('identifies "explain" tasks as research', () => {
      const analysis = analyzeTask('Explain the data flow in the pipeline')
      expect(analysis.taskType).toBe('research')
    })

    it('identifies debug tasks', () => {
      const analysis = analyzeTask('Fix the bug in the login handler')
      expect(analysis.taskType).toBe('debug')
      expect(analysis.confidence).toBeGreaterThanOrEqual(0.5)
    })

    it('identifies "error" tasks as debug', () => {
      const analysis = analyzeTask('There is an error in the build process')
      expect(analysis.taskType).toBe('debug')
    })

    it('identifies write/implement tasks', () => {
      const analysis = analyzeTask('Implement user authentication')
      expect(analysis.taskType).toBe('write')
      expect(analysis.confidence).toBeGreaterThanOrEqual(0.5)
    })

    it('identifies "create" tasks as write', () => {
      const analysis = analyzeTask('Create a new component for the dashboard')
      expect(analysis.taskType).toBe('write')
    })

    it('identifies "refactor" tasks as write', () => {
      const analysis = analyzeTask('Refactor the database module')
      expect(analysis.taskType).toBe('write')
    })

    it('returns general for ambiguous tasks', () => {
      const analysis = analyzeTask('hello world')
      expect(analysis.taskType).toBe('general')
      expect(analysis.confidence).toBe(0)
    })

    it('returns general for empty goal', () => {
      const analysis = analyzeTask('')
      expect(analysis.taskType).toBe('general')
      expect(analysis.confidence).toBe(0)
    })

    it('detects code paths in text', () => {
      const analysis = analyzeTask('Fix the bug in src/components/App.tsx')
      expect(analysis.hasCodePaths).toBe(true)
    })

    it('detects no code paths in plain text', () => {
      const analysis = analyzeTask('Review the login flow')
      expect(analysis.hasCodePaths).toBe(false)
    })

    it('uses context for analysis', () => {
      const analysis = analyzeTask('Fix this', 'There is a bug in the error handler')
      expect(analysis.taskType).toBe('debug')
    })

    it('increases confidence with multiple keyword matches', () => {
      const singleMatch = analyzeTask('Fix something')
      const multiMatch = analyzeTask('Fix the error and debug the crash')
      expect(multiMatch.confidence).toBeGreaterThanOrEqual(singleMatch.confidence)
    })
  })

  describe('selectWorker', () => {
    it('selects coder for write tasks', () => {
      const registry = createTestRegistry()
      const analysis = analyzeTask('Implement a new feature')
      const worker = selectWorker(analysis, registry)
      expect(worker).not.toBeNull()
      expect(worker!.name).toBe('coder')
    })

    it('selects tester for test tasks', () => {
      const registry = createTestRegistry()
      const analysis = analyzeTask('Write tests for the API')
      const worker = selectWorker(analysis, registry)
      expect(worker).not.toBeNull()
      expect(worker!.name).toBe('tester')
    })

    it('selects reviewer for review tasks', () => {
      const registry = createTestRegistry()
      const analysis = analyzeTask('Review the authentication code')
      const worker = selectWorker(analysis, registry)
      expect(worker).not.toBeNull()
      expect(worker!.name).toBe('reviewer')
    })

    it('selects researcher for research tasks', () => {
      const registry = createTestRegistry()
      const analysis = analyzeTask('Research how the caching works')
      const worker = selectWorker(analysis, registry)
      expect(worker).not.toBeNull()
      expect(worker!.name).toBe('researcher')
    })

    it('selects debugger for debug tasks', () => {
      const registry = createTestRegistry()
      const analysis = analyzeTask('Fix the crash in production')
      const worker = selectWorker(analysis, registry)
      expect(worker).not.toBeNull()
      expect(worker!.name).toBe('debugger')
    })

    it('returns null for general tasks', () => {
      const registry = createTestRegistry()
      const analysis = analyzeTask('hello world')
      const worker = selectWorker(analysis, registry)
      expect(worker).toBeNull()
    })

    it('returns null for low confidence', () => {
      const registry = createTestRegistry()
      const analysis = {
        keywords: [],
        hasCodePaths: false,
        taskType: 'write' as const,
        confidence: 0.3,
      }
      const worker = selectWorker(analysis, registry)
      expect(worker).toBeNull()
    })

    it('returns null if worker not in registry', () => {
      const registry = createWorkerRegistry() // empty registry
      const analysis = analyzeTask('Write tests for auth')
      const worker = selectWorker(analysis, registry)
      expect(worker).toBeNull()
    })
  })
})
