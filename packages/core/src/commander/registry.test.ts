/**
 * Worker Registry Tests
 *
 * Tests for WorkerRegistry: register, lookup, phone book generation, and factory.
 */

import { describe, expect, it } from 'vitest'
import { createWorkerRegistry, WorkerRegistry } from './registry.js'
import type { WorkerDefinition } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

function makeWorker(name: string, overrides: Partial<WorkerDefinition> = {}): WorkerDefinition {
  return {
    name,
    displayName: name.charAt(0).toUpperCase() + name.slice(1),
    description: `${name} worker for testing`,
    systemPrompt: `You are the ${name} worker.`,
    tools: ['read', 'write'],
    ...overrides,
  }
}

// ============================================================================
// WorkerRegistry
// ============================================================================

describe('WorkerRegistry', () => {
  // ==========================================================================
  // Registration
  // ==========================================================================

  describe('register', () => {
    it('registers a worker', () => {
      const registry = new WorkerRegistry()
      registry.register(makeWorker('coder'))

      expect(registry.has('coder')).toBe(true)
      expect(registry.size).toBe(1)
    })

    it('overwrites on duplicate registration', () => {
      const registry = new WorkerRegistry()
      registry.register(makeWorker('coder', { description: 'original' }))
      registry.register(makeWorker('coder', { description: 'updated' }))

      expect(registry.size).toBe(1)
      expect(registry.get('coder')!.description).toBe('updated')
    })
  })

  describe('registerAll', () => {
    it('registers multiple workers', () => {
      const registry = new WorkerRegistry()
      registry.registerAll([makeWorker('coder'), makeWorker('tester'), makeWorker('reviewer')])

      expect(registry.size).toBe(3)
      expect(registry.has('coder')).toBe(true)
      expect(registry.has('tester')).toBe(true)
      expect(registry.has('reviewer')).toBe(true)
    })
  })

  // ==========================================================================
  // Lookup
  // ==========================================================================

  describe('get', () => {
    it('returns worker definition', () => {
      const registry = new WorkerRegistry()
      const worker = makeWorker('coder')
      registry.register(worker)

      expect(registry.get('coder')).toBe(worker)
    })

    it('returns undefined for non-existent worker', () => {
      const registry = new WorkerRegistry()
      expect(registry.get('nonexistent')).toBeUndefined()
    })
  })

  describe('has', () => {
    it('returns true for registered worker', () => {
      const registry = new WorkerRegistry()
      registry.register(makeWorker('coder'))
      expect(registry.has('coder')).toBe(true)
    })

    it('returns false for unregistered worker', () => {
      const registry = new WorkerRegistry()
      expect(registry.has('nonexistent')).toBe(false)
    })
  })

  describe('getAllWorkers', () => {
    it('returns all registered workers', () => {
      const registry = new WorkerRegistry()
      registry.registerAll([makeWorker('coder'), makeWorker('tester')])

      const workers = registry.getAllWorkers()
      expect(workers).toHaveLength(2)
      expect(workers.map((w) => w.name)).toContain('coder')
      expect(workers.map((w) => w.name)).toContain('tester')
    })

    it('returns empty array when no workers', () => {
      const registry = new WorkerRegistry()
      expect(registry.getAllWorkers()).toEqual([])
    })
  })

  describe('getWorkerNames', () => {
    it('returns all worker names', () => {
      const registry = new WorkerRegistry()
      registry.registerAll([makeWorker('coder'), makeWorker('tester')])

      const names = registry.getWorkerNames()
      expect(names).toHaveLength(2)
      expect(names).toContain('coder')
      expect(names).toContain('tester')
    })
  })

  // ==========================================================================
  // Removal
  // ==========================================================================

  describe('unregister', () => {
    it('removes worker and returns true', () => {
      const registry = new WorkerRegistry()
      registry.register(makeWorker('coder'))

      expect(registry.unregister('coder')).toBe(true)
      expect(registry.has('coder')).toBe(false)
      expect(registry.size).toBe(0)
    })

    it('returns false for non-existent worker', () => {
      const registry = new WorkerRegistry()
      expect(registry.unregister('nonexistent')).toBe(false)
    })
  })

  describe('clear', () => {
    it('removes all workers', () => {
      const registry = new WorkerRegistry()
      registry.registerAll([makeWorker('coder'), makeWorker('tester')])

      registry.clear()

      expect(registry.size).toBe(0)
      expect(registry.has('coder')).toBe(false)
    })
  })

  // ==========================================================================
  // Phone Book / Directory Context
  // ==========================================================================

  describe('getDirectoryContext', () => {
    it('returns empty string when no workers', () => {
      const registry = new WorkerRegistry()
      expect(registry.getDirectoryContext()).toBe('')
    })

    it('includes worker name and description', () => {
      const registry = new WorkerRegistry()
      registry.register(makeWorker('coder', { description: 'Writes clean code' }))

      const context = registry.getDirectoryContext()
      expect(context).toContain('Coder')
      expect(context).toContain('delegate_coder')
      expect(context).toContain('Writes clean code')
    })

    it('includes available tools', () => {
      const registry = new WorkerRegistry()
      registry.register(makeWorker('coder', { tools: ['read', 'write', 'grep'] }))

      const context = registry.getDirectoryContext()
      expect(context).toContain('read, write, grep')
    })

    it('includes max turns', () => {
      const registry = new WorkerRegistry()
      registry.register(makeWorker('coder', { maxTurns: 15 }))

      const context = registry.getDirectoryContext()
      expect(context).toContain('15')
    })

    it('includes delegation guidelines', () => {
      const registry = new WorkerRegistry()
      registry.register(makeWorker('coder'))

      const context = registry.getDirectoryContext()
      expect(context).toContain('Delegation Guidelines')
      expect(context).toContain('Always delegate')
    })

    it('lists all workers when multiple registered', () => {
      const registry = new WorkerRegistry()
      registry.registerAll([makeWorker('coder'), makeWorker('tester'), makeWorker('reviewer')])

      const context = registry.getDirectoryContext()
      expect(context).toContain('delegate_coder')
      expect(context).toContain('delegate_tester')
      expect(context).toContain('delegate_reviewer')
    })
  })

  describe('getSummary', () => {
    it('returns "No workers available." when empty', () => {
      const registry = new WorkerRegistry()
      expect(registry.getSummary()).toBe('No workers available.')
    })

    it('returns compact list of workers', () => {
      const registry = new WorkerRegistry()
      registry.registerAll([makeWorker('coder'), makeWorker('tester')])

      const summary = registry.getSummary()
      expect(summary).toContain('Coder (coder)')
      expect(summary).toContain('Tester (tester)')
    })
  })
})

// ============================================================================
// Factory
// ============================================================================

describe('createWorkerRegistry', () => {
  it('creates new registry instance', () => {
    const a = createWorkerRegistry()
    const b = createWorkerRegistry()
    expect(a).not.toBe(b)
    expect(a.size).toBe(0)
  })
})
