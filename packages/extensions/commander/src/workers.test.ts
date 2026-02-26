/**
 * Built-in worker definitions.
 */

import { describe, expect, it } from 'vitest'
import {
  BUILTIN_WORKERS,
  CODER_WORKER,
  DEBUGGER_WORKER,
  RESEARCHER_WORKER,
  REVIEWER_WORKER,
  TESTER_WORKER,
} from './workers.js'

describe('BUILTIN_WORKERS', () => {
  it('contains exactly 5 workers', () => {
    expect(BUILTIN_WORKERS).toHaveLength(5)
  })

  it('includes all expected workers', () => {
    const names = BUILTIN_WORKERS.map((w) => w.name)
    expect(names).toContain('coder')
    expect(names).toContain('tester')
    expect(names).toContain('reviewer')
    expect(names).toContain('researcher')
    expect(names).toContain('debugger')
  })
})

describe('worker definitions', () => {
  const workers = [CODER_WORKER, TESTER_WORKER, REVIEWER_WORKER, RESEARCHER_WORKER, DEBUGGER_WORKER]

  for (const worker of workers) {
    describe(worker.displayName, () => {
      it('has required fields', () => {
        expect(worker.name).toBeDefined()
        expect(worker.displayName).toBeDefined()
        expect(worker.description).toBeDefined()
        expect(worker.systemPrompt).toBeDefined()
        expect(Array.isArray(worker.tools)).toBe(true)
        expect(worker.tools.length).toBeGreaterThan(0)
      })

      it('has reasonable maxTurns', () => {
        expect(worker.maxTurns).toBeDefined()
        expect(worker.maxTurns).toBeGreaterThan(0)
        expect(worker.maxTurns).toBeLessThanOrEqual(20)
      })

      it('has reasonable maxTimeMinutes', () => {
        expect(worker.maxTimeMinutes).toBeDefined()
        expect(worker.maxTimeMinutes).toBeGreaterThan(0)
        expect(worker.maxTimeMinutes).toBeLessThanOrEqual(10)
      })
    })
  }
})

describe('CODER_WORKER', () => {
  it('has write tools', () => {
    expect(CODER_WORKER.tools).toContain('write_file')
    expect(CODER_WORKER.tools).toContain('edit')
    expect(CODER_WORKER.tools).toContain('create_file')
  })
})

describe('REVIEWER_WORKER', () => {
  it('has read-only tools', () => {
    expect(REVIEWER_WORKER.tools).toContain('read_file')
    expect(REVIEWER_WORKER.tools).toContain('grep')
    expect(REVIEWER_WORKER.tools).toContain('glob')
    expect(REVIEWER_WORKER.tools).not.toContain('write_file')
    expect(REVIEWER_WORKER.tools).not.toContain('edit')
    expect(REVIEWER_WORKER.tools).not.toContain('bash')
  })
})

describe('TESTER_WORKER', () => {
  it('has bash for running tests', () => {
    expect(TESTER_WORKER.tools).toContain('bash')
  })

  it('has write_file for creating tests', () => {
    expect(TESTER_WORKER.tools).toContain('write_file')
    expect(TESTER_WORKER.tools).toContain('create_file')
  })
})

describe('RESEARCHER_WORKER', () => {
  it('has read-only plus ls', () => {
    expect(RESEARCHER_WORKER.tools).toContain('read_file')
    expect(RESEARCHER_WORKER.tools).toContain('ls')
    expect(RESEARCHER_WORKER.tools).not.toContain('write_file')
  })
})

describe('DEBUGGER_WORKER', () => {
  it('has bash and write tools for debugging', () => {
    expect(DEBUGGER_WORKER.tools).toContain('bash')
    expect(DEBUGGER_WORKER.tools).toContain('write_file')
    expect(DEBUGGER_WORKER.tools).toContain('edit')
  })
})
