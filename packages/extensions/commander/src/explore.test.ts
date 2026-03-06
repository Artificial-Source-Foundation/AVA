/**
 * Explore worker — read-only subagent tests.
 */

import { describe, expect, it } from 'vitest'
import { resolveTools } from './delegate.js'
import { EXPLORE_ALLOWED_TOOLS, EXPLORE_DENIED_TOOLS, EXPLORE_WORKER } from './explore.js'

describe('EXPLORE_WORKER', () => {
  it('has correct identity fields', () => {
    expect(EXPLORE_WORKER.id).toBe('explorer')
    expect(EXPLORE_WORKER.name).toBe('explorer')
    expect(EXPLORE_WORKER.displayName).toBe('Explorer')
    expect(EXPLORE_WORKER.tier).toBe('worker')
    expect(EXPLORE_WORKER.isBuiltIn).toBe(true)
  })

  it('description mentions read-only', () => {
    expect(EXPLORE_WORKER.description).toContain('read-only')
  })

  it('system prompt emphasizes read-only access', () => {
    expect(EXPLORE_WORKER.systemPrompt).toContain('read-only')
    expect(EXPLORE_WORKER.systemPrompt).toContain('cannot modify')
  })

  it('has only read-only tools in allowed list', () => {
    const writeTools = ['write_file', 'create_file', 'delete_file', 'edit', 'bash', 'apply_patch']
    for (const tool of writeTools) {
      expect(EXPLORE_WORKER.tools).not.toContain(tool)
    }
  })

  it('includes expected read-only tools', () => {
    expect(EXPLORE_WORKER.tools).toContain('read_file')
    expect(EXPLORE_WORKER.tools).toContain('glob')
    expect(EXPLORE_WORKER.tools).toContain('grep')
    expect(EXPLORE_WORKER.tools).toContain('ls')
    expect(EXPLORE_WORKER.tools).toContain('websearch')
    expect(EXPLORE_WORKER.tools).toContain('webfetch')
  })

  it('has deniedTools list', () => {
    expect(EXPLORE_WORKER.deniedTools).toBeDefined()
    expect(EXPLORE_WORKER.deniedTools!.length).toBeGreaterThan(0)
  })

  it('deniedTools includes all write/exec tools', () => {
    const denied = EXPLORE_WORKER.deniedTools!
    expect(denied).toContain('write_file')
    expect(denied).toContain('create_file')
    expect(denied).toContain('delete_file')
    expect(denied).toContain('edit')
    expect(denied).toContain('bash')
    expect(denied).toContain('apply_patch')
    expect(denied).toContain('multiedit')
    expect(denied).toContain('task')
  })

  it('deniedTools includes delegation tools', () => {
    const denied = EXPLORE_WORKER.deniedTools!
    expect(denied).toContain('delegate_coder')
    expect(denied).toContain('delegate_reviewer')
    expect(denied).toContain('delegate_researcher')
    expect(denied).toContain('delegate_explorer')
  })

  it('has no overlap between allowed and denied tools', () => {
    const allowed = new Set<string>(EXPLORE_ALLOWED_TOOLS)
    const denied = new Set<string>(EXPLORE_DENIED_TOOLS)
    for (const tool of allowed) {
      expect(denied.has(tool)).toBe(false)
    }
  })

  it('has reasonable maxTurns', () => {
    expect(EXPLORE_WORKER.maxTurns).toBeDefined()
    expect(EXPLORE_WORKER.maxTurns).toBeGreaterThan(0)
    expect(EXPLORE_WORKER.maxTurns).toBeLessThanOrEqual(20)
  })

  it('has reasonable maxTimeMinutes', () => {
    expect(EXPLORE_WORKER.maxTimeMinutes).toBeDefined()
    expect(EXPLORE_WORKER.maxTimeMinutes).toBeGreaterThan(0)
    expect(EXPLORE_WORKER.maxTimeMinutes).toBeLessThanOrEqual(10)
  })

  it('has domain and capabilities', () => {
    expect(EXPLORE_WORKER.domain).toBe('fullstack')
    expect(EXPLORE_WORKER.capabilities).toBeDefined()
    expect(EXPLORE_WORKER.capabilities!.length).toBeGreaterThan(0)
    expect(EXPLORE_WORKER.capabilities).toContain('read-only-analysis')
  })
})

describe('resolveTools with deniedTools', () => {
  it('filters out deniedTools from worker', () => {
    const tools = resolveTools(EXPLORE_WORKER)
    // Only allowed tools should remain
    for (const tool of EXPLORE_DENIED_TOOLS) {
      expect(tools).not.toContain(tool)
    }
  })

  it('keeps allowed tools after filtering', () => {
    const tools = resolveTools(EXPLORE_WORKER)
    expect(tools).toContain('read_file')
    expect(tools).toContain('glob')
    expect(tools).toContain('grep')
  })

  it('does not affect agents without deniedTools', () => {
    const agent = {
      id: 'normal',
      name: 'normal',
      displayName: 'Normal',
      description: '',
      tier: 'worker' as const,
      systemPrompt: '',
      tools: ['read_file', 'write_file', 'edit', 'delegate_tester'],
    }
    const tools = resolveTools(agent)
    // Workers within depth limit keep all tools including delegate_
    expect(tools).toContain('read_file')
    expect(tools).toContain('write_file')
    expect(tools).toContain('edit')
    expect(tools).toContain('delegate_tester')
  })
})
