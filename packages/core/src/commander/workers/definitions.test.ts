/**
 * Built-in Worker Definitions Tests
 *
 * Tests that all built-in workers are properly defined and createDefaultRegistry works.
 */

import { describe, expect, it } from 'vitest'
import {
  BUILT_IN_WORKERS,
  CODER_WORKER,
  createDefaultRegistry,
  DEBUGGER_WORKER,
  RESEARCHER_WORKER,
  REVIEWER_WORKER,
  TESTER_WORKER,
} from './definitions.js'

// ============================================================================
// Individual Worker Definitions
// ============================================================================

describe('built-in worker definitions', () => {
  it('CODER_WORKER has correct structure', () => {
    expect(CODER_WORKER.name).toBe('coder')
    expect(CODER_WORKER.displayName).toBe('Coder')
    expect(CODER_WORKER.tools).toContain('read')
    expect(CODER_WORKER.tools).toContain('write')
    expect(CODER_WORKER.maxTurns).toBe(15)
    expect(CODER_WORKER.systemPrompt).toContain('software developer')
  })

  it('TESTER_WORKER has correct structure', () => {
    expect(TESTER_WORKER.name).toBe('tester')
    expect(TESTER_WORKER.tools).toContain('bash')
    expect(TESTER_WORKER.systemPrompt).toContain('QA')
  })

  it('REVIEWER_WORKER is read-only', () => {
    expect(REVIEWER_WORKER.name).toBe('reviewer')
    expect(REVIEWER_WORKER.tools).not.toContain('write')
    expect(REVIEWER_WORKER.tools).not.toContain('bash')
    expect(REVIEWER_WORKER.tools).toContain('read')
    expect(REVIEWER_WORKER.tools).toContain('grep')
  })

  it('RESEARCHER_WORKER is read-only', () => {
    expect(RESEARCHER_WORKER.name).toBe('researcher')
    expect(RESEARCHER_WORKER.tools).not.toContain('write')
    expect(RESEARCHER_WORKER.tools).not.toContain('bash')
    expect(RESEARCHER_WORKER.tools).toContain('read')
  })

  it('DEBUGGER_WORKER has write and bash access', () => {
    expect(DEBUGGER_WORKER.name).toBe('debugger')
    expect(DEBUGGER_WORKER.tools).toContain('write')
    expect(DEBUGGER_WORKER.tools).toContain('bash')
  })

  it('all workers have required fields', () => {
    for (const worker of BUILT_IN_WORKERS) {
      expect(worker.name).toBeTruthy()
      expect(worker.displayName).toBeTruthy()
      expect(worker.description).toBeTruthy()
      expect(worker.systemPrompt).toBeTruthy()
      expect(worker.tools.length).toBeGreaterThan(0)
    }
  })

  it('all worker names are unique', () => {
    const names = BUILT_IN_WORKERS.map((w) => w.name)
    const unique = new Set(names)
    expect(unique.size).toBe(names.length)
  })

  it('no worker has delegate_* tools', () => {
    for (const worker of BUILT_IN_WORKERS) {
      const delegateTools = worker.tools.filter((t) => t.startsWith('delegate_'))
      expect(delegateTools).toEqual([])
    }
  })
})

// ============================================================================
// BUILT_IN_WORKERS Array
// ============================================================================

describe('BUILT_IN_WORKERS', () => {
  it('contains exactly 5 workers', () => {
    expect(BUILT_IN_WORKERS).toHaveLength(5)
  })

  it('contains all expected workers', () => {
    const names = BUILT_IN_WORKERS.map((w) => w.name)
    expect(names).toContain('coder')
    expect(names).toContain('tester')
    expect(names).toContain('reviewer')
    expect(names).toContain('researcher')
    expect(names).toContain('debugger')
  })
})

// ============================================================================
// Factory
// ============================================================================

describe('createDefaultRegistry', () => {
  it('creates registry with all built-in workers', () => {
    const registry = createDefaultRegistry()

    expect(registry.size).toBe(5)
    expect(registry.has('coder')).toBe(true)
    expect(registry.has('tester')).toBe(true)
    expect(registry.has('reviewer')).toBe(true)
    expect(registry.has('researcher')).toBe(true)
    expect(registry.has('debugger')).toBe(true)
  })

  it('creates independent instances', () => {
    const a = createDefaultRegistry()
    const b = createDefaultRegistry()
    expect(a).not.toBe(b)
  })
})
