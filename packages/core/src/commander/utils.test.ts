/**
 * Commander Utilities Tests
 *
 * Tests for phone book generation, output formatting,
 * result aggregation, error handling, and statistics.
 */

import { describe, expect, it } from 'vitest'
import { AgentTerminateMode } from '../agent/types.js'
import { WorkerRegistry } from './registry.js'
import type { WorkerDefinition, WorkerResult } from './types.js'
import {
  aggregateErrors,
  calculateWorkerStats,
  combineWorkerResults,
  formatAggregatedErrors,
  formatWorkerOutput,
  formatWorkerSummary,
  generateCompactPhoneBook,
  generatePhoneBook,
  getFailedWorkers,
  hasWorkerFailures,
} from './utils.js'

// ============================================================================
// Helpers
// ============================================================================

function makeWorker(name: string): WorkerDefinition {
  return {
    name,
    displayName: name.charAt(0).toUpperCase() + name.slice(1),
    description: `${name} worker for testing. It does ${name} things.`,
    systemPrompt: `You are the ${name} worker.`,
    tools: ['read', 'write'],
  }
}

function makeResult(overrides: Partial<WorkerResult> = {}): WorkerResult {
  return {
    success: true,
    output: 'Task completed successfully.',
    terminateMode: AgentTerminateMode.COMPLETE,
    tokensUsed: 1000,
    durationMs: 5000,
    turns: 3,
    ...overrides,
  }
}

function makeEntry(worker: string, overrides: Partial<WorkerResult> = {}) {
  return { worker, result: makeResult(overrides) }
}

// ============================================================================
// Phone Book Generation
// ============================================================================

describe('generatePhoneBook', () => {
  it('returns empty string for empty registry', () => {
    const registry = new WorkerRegistry()
    expect(generatePhoneBook(registry)).toBe('')
  })

  it('generates phone book with workers', () => {
    const registry = new WorkerRegistry()
    registry.registerAll([makeWorker('coder'), makeWorker('tester')])

    const result = generatePhoneBook(registry)
    expect(result).toContain('Available Workers')
    expect(result).toContain('delegate_coder')
  })
})

describe('generateCompactPhoneBook', () => {
  it('returns "No workers available." for empty registry', () => {
    const registry = new WorkerRegistry()
    expect(generateCompactPhoneBook(registry)).toBe('No workers available.')
  })

  it('generates compact list', () => {
    const registry = new WorkerRegistry()
    registry.registerAll([makeWorker('coder'), makeWorker('tester')])

    const result = generateCompactPhoneBook(registry)
    expect(result).toContain('delegate_coder')
    expect(result).toContain('delegate_tester')
    // Should be brief — first sentence only
    expect(result).toContain('coder worker for testing.')
  })
})

// ============================================================================
// Output Formatting
// ============================================================================

describe('formatWorkerOutput', () => {
  it('formats successful result', () => {
    const output = formatWorkerOutput('coder', makeResult())

    expect(output).toContain('coder')
    expect(output).toContain('SUCCESS')
    expect(output).toContain('Task completed successfully')
    expect(output).toContain('Turns: 3')
    expect(output).toContain('Tokens: 1000')
    expect(output).toContain('5000ms')
  })

  it('formats failed result with error', () => {
    const output = formatWorkerOutput(
      'tester',
      makeResult({
        success: false,
        error: 'Tests failed',
        terminateMode: AgentTerminateMode.ERROR,
      })
    )

    expect(output).toContain('FAILED')
    expect(output).toContain('Tests failed')
  })
})

describe('formatWorkerSummary', () => {
  it('formats brief success summary', () => {
    const summary = formatWorkerSummary('coder', makeResult())

    expect(summary).toContain('coder')
    expect(summary).toContain('completed')
  })

  it('formats brief failure summary', () => {
    const summary = formatWorkerSummary('tester', makeResult({ success: false }))

    expect(summary).toContain('failed')
  })

  it('truncates long output', () => {
    const longOutput = 'A'.repeat(200)
    const summary = formatWorkerSummary('coder', makeResult({ output: longOutput }))

    expect(summary).toContain('...')
  })
})

// ============================================================================
// Result Aggregation
// ============================================================================

describe('combineWorkerResults', () => {
  it('combines empty results', () => {
    const combined = combineWorkerResults([])

    expect(combined.success).toBe(true)
    expect(combined.summary).toContain('No workers')
    expect(combined.results).toEqual([])
    expect(combined.totalTokensUsed).toBe(0)
  })

  it('combines all-success results', () => {
    const combined = combineWorkerResults([
      makeEntry('coder', { tokensUsed: 1000, durationMs: 5000 }),
      makeEntry('tester', { tokensUsed: 2000, durationMs: 3000 }),
    ])

    expect(combined.success).toBe(true)
    expect(combined.totalTokensUsed).toBe(3000)
    expect(combined.totalDurationMs).toBe(8000)
    expect(combined.results).toHaveLength(2)
    expect(combined.summary).toContain('All 2 worker(s) completed successfully')
  })

  it('combines mixed results', () => {
    const combined = combineWorkerResults([
      makeEntry('coder'),
      makeEntry('tester', { success: false, error: 'Tests failed' }),
    ])

    expect(combined.success).toBe(false)
    expect(combined.summary).toContain('1 worker(s) succeeded')
    expect(combined.summary).toContain('1 failed')
  })

  it('combines all-failure results', () => {
    const combined = combineWorkerResults([
      makeEntry('coder', { success: false }),
      makeEntry('tester', { success: false }),
    ])

    expect(combined.success).toBe(false)
    expect(combined.summary).toContain('All 2 worker(s) failed')
  })
})

// ============================================================================
// Error Handling
// ============================================================================

describe('aggregateErrors', () => {
  it('returns empty for all-success results', () => {
    const errors = aggregateErrors([makeEntry('coder'), makeEntry('tester')])
    expect(errors).toEqual([])
  })

  it('collects error messages', () => {
    const errors = aggregateErrors([
      makeEntry('coder', { success: false, error: 'Syntax error' }),
      makeEntry('tester', { success: false, error: 'Test timeout' }),
    ])

    expect(errors).toHaveLength(2)
    expect(errors[0]).toContain('[coder]')
    expect(errors[0]).toContain('Syntax error')
    expect(errors[1]).toContain('[tester]')
  })
})

describe('formatAggregatedErrors', () => {
  it('returns null when no errors', () => {
    expect(formatAggregatedErrors([makeEntry('coder')])).toBeNull()
  })

  it('formats errors as markdown', () => {
    const formatted = formatAggregatedErrors([
      makeEntry('coder', { success: false, error: 'Failed' }),
    ])

    expect(formatted).toContain('Worker Errors')
    expect(formatted).toContain('[coder] Failed')
  })
})

describe('hasWorkerFailures', () => {
  it('returns false when all succeed', () => {
    expect(hasWorkerFailures([makeEntry('coder')])).toBe(false)
  })

  it('returns true when any fail', () => {
    expect(hasWorkerFailures([makeEntry('coder'), makeEntry('tester', { success: false })])).toBe(
      true
    )
  })
})

describe('getFailedWorkers', () => {
  it('returns empty when all succeed', () => {
    expect(getFailedWorkers([makeEntry('coder')])).toEqual([])
  })

  it('returns failed worker names', () => {
    const failed = getFailedWorkers([
      makeEntry('coder'),
      makeEntry('tester', { success: false }),
      makeEntry('reviewer', { success: false }),
    ])

    expect(failed).toEqual(['tester', 'reviewer'])
  })
})

// ============================================================================
// Statistics
// ============================================================================

describe('calculateWorkerStats', () => {
  it('calculates stats for empty results', () => {
    const stats = calculateWorkerStats([])

    expect(stats.totalWorkers).toBe(0)
    expect(stats.averageTokensPerWorker).toBe(0)
  })

  it('calculates correct aggregates', () => {
    const stats = calculateWorkerStats([
      makeEntry('coder', { tokensUsed: 1000, durationMs: 5000 }),
      makeEntry('tester', { tokensUsed: 2000, durationMs: 3000, success: false }),
    ])

    expect(stats.totalWorkers).toBe(2)
    expect(stats.successfulWorkers).toBe(1)
    expect(stats.failedWorkers).toBe(1)
    expect(stats.totalTokens).toBe(3000)
    expect(stats.totalDurationMs).toBe(8000)
    expect(stats.averageTokensPerWorker).toBe(1500)
    expect(stats.averageDurationPerWorker).toBe(4000)
  })
})
