/**
 * Built-in agent definitions — Praxis hierarchy.
 */

import { describe, expect, it } from 'vitest'
import {
  BUILTIN_AGENTS,
  BUILTIN_WORKERS,
  COMMANDER_AGENT,
  LEAD_AGENTS,
  WORKER_AGENTS,
} from './workers.js'

describe('BUILTIN_AGENTS', () => {
  it('contains 13 agents total (1 commander + 4 leads + 8 workers)', () => {
    expect(BUILTIN_AGENTS).toHaveLength(13)
  })

  it('has 1 commander, 4 leads, and 8 workers', () => {
    const commanders = BUILTIN_AGENTS.filter((a) => a.tier === 'commander')
    const leads = BUILTIN_AGENTS.filter((a) => a.tier === 'lead')
    const workers = BUILTIN_AGENTS.filter((a) => a.tier === 'worker')

    expect(commanders).toHaveLength(1)
    expect(leads).toHaveLength(4)
    expect(workers).toHaveLength(8)
  })
})

describe('WORKER_AGENTS', () => {
  it('contains 8 workers', () => {
    expect(WORKER_AGENTS).toHaveLength(8)
  })

  it('includes all expected workers', () => {
    const names = WORKER_AGENTS.map((w) => w.name)
    expect(names).toContain('coder')
    expect(names).toContain('tester')
    expect(names).toContain('reviewer')
    expect(names).toContain('researcher')
    expect(names).toContain('debugger')
    expect(names).toContain('architect')
    expect(names).toContain('planner')
    expect(names).toContain('devops')
  })

  for (const worker of WORKER_AGENTS) {
    describe(worker.displayName, () => {
      it('has required fields', () => {
        expect(worker.name).toBeDefined()
        expect(worker.displayName).toBeDefined()
        expect(worker.description).toBeDefined()
        expect(worker.systemPrompt).toBeDefined()
        expect(Array.isArray(worker.tools)).toBe(true)
        expect(worker.tools.length).toBeGreaterThan(0)
        expect(worker.tier).toBe('worker')
        expect(worker.isBuiltIn).toBe(true)
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

describe('LEAD_AGENTS', () => {
  it('contains 4 leads', () => {
    expect(LEAD_AGENTS).toHaveLength(4)
  })

  for (const lead of LEAD_AGENTS) {
    describe(lead.displayName, () => {
      it('has tier lead', () => {
        expect(lead.tier).toBe('lead')
      })

      it('has delegates list', () => {
        expect(lead.delegates).toBeDefined()
        expect(lead.delegates!.length).toBeGreaterThan(0)
      })

      it('delegates to valid worker IDs', () => {
        const workerIds = WORKER_AGENTS.map((w) => w.id)
        for (const delegateId of lead.delegates!) {
          expect(workerIds).toContain(delegateId)
        }
      })
    })
  }
})

describe('COMMANDER_AGENT', () => {
  it('has tier commander', () => {
    expect(COMMANDER_AGENT.tier).toBe('commander')
  })

  it('has no coding tools', () => {
    expect(COMMANDER_AGENT.tools).not.toContain('read_file')
    expect(COMMANDER_AGENT.tools).not.toContain('write_file')
    expect(COMMANDER_AGENT.tools).not.toContain('bash')
  })

  it('has meta tools only', () => {
    expect(COMMANDER_AGENT.tools).toContain('question')
    expect(COMMANDER_AGENT.tools).toContain('attempt_completion')
  })

  it('delegates to leads and planning agents', () => {
    expect(COMMANDER_AGENT.delegates).toContain('frontend-lead')
    expect(COMMANDER_AGENT.delegates).toContain('backend-lead')
    expect(COMMANDER_AGENT.delegates).toContain('planner')
    expect(COMMANDER_AGENT.delegates).toContain('architect')
  })
})

describe('BUILTIN_WORKERS (legacy compat)', () => {
  it('contains original 5 workers as WorkerDefinition[]', () => {
    expect(BUILTIN_WORKERS).toHaveLength(5)
    const names = BUILTIN_WORKERS.map((w) => w.name)
    expect(names).toContain('coder')
    expect(names).toContain('tester')
    expect(names).toContain('reviewer')
    expect(names).toContain('researcher')
    expect(names).toContain('debugger')
  })
})

describe('specific workers', () => {
  const coder = WORKER_AGENTS.find((a) => a.id === 'coder')!
  const reviewer = WORKER_AGENTS.find((a) => a.id === 'reviewer')!
  const tester = WORKER_AGENTS.find((a) => a.id === 'tester')!
  const researcher = WORKER_AGENTS.find((a) => a.id === 'researcher')!
  const debugger_ = WORKER_AGENTS.find((a) => a.id === 'debugger')!

  it('coder has write tools', () => {
    expect(coder.tools).toContain('write_file')
    expect(coder.tools).toContain('edit')
    expect(coder.tools).toContain('create_file')
  })

  it('reviewer has read-only tools', () => {
    expect(reviewer.tools).toContain('read_file')
    expect(reviewer.tools).toContain('grep')
    expect(reviewer.tools).not.toContain('write_file')
    expect(reviewer.tools).not.toContain('edit')
    expect(reviewer.tools).not.toContain('bash')
  })

  it('tester has bash and write tools', () => {
    expect(tester.tools).toContain('bash')
    expect(tester.tools).toContain('write_file')
    expect(tester.tools).toContain('create_file')
  })

  it('researcher has read-only plus ls', () => {
    expect(researcher.tools).toContain('read_file')
    expect(researcher.tools).toContain('ls')
    expect(researcher.tools).not.toContain('write_file')
  })

  it('debugger has bash and edit', () => {
    expect(debugger_.tools).toContain('bash')
    expect(debugger_.tools).toContain('write_file')
    expect(debugger_.tools).toContain('edit')
  })
})
