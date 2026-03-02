/**
 * Agent mode selector tests.
 */

import { describe, expect, it } from 'vitest'
import { selectAgentMode } from './selector.js'

function makeModes(...names: string[]): ReadonlyMap<string, { name: string }> {
  return new Map(names.map((n) => [n, { name: n }]))
}

describe('selectAgentMode', () => {
  it('returns undefined when praxis mode is not available', () => {
    const result = selectAgentMode('refactor everything', makeModes('plan', 'minimal'))
    expect(result).toBeUndefined()
  })

  it('returns "praxis" for complex keywords', () => {
    const modes = makeModes('praxis', 'plan', 'minimal')
    expect(selectAgentMode('refactor the auth module', modes)).toBe('praxis')
    expect(selectAgentMode('migrate database to PostgreSQL', modes)).toBe('praxis')
    expect(selectAgentMode('redesign the settings page', modes)).toBe('praxis')
    expect(selectAgentMode('architect a new plugin system', modes)).toBe('praxis')
    expect(selectAgentMode('update frontend and backend', modes)).toBe('praxis')
    expect(selectAgentMode('audit the entire codebase', modes)).toBe('praxis')
    expect(selectAgentMode('overhaul the build pipeline', modes)).toBe('praxis')
    expect(selectAgentMode('comprehensive test coverage', modes)).toBe('praxis')
    expect(selectAgentMode('work across the codebase', modes)).toBe('praxis')
    expect(selectAgentMode('end-to-end testing setup', modes)).toBe('praxis')
    expect(selectAgentMode('change multiple files at once', modes)).toBe('praxis')
  })

  it('is case-insensitive for keyword matching', () => {
    const modes = makeModes('praxis')
    expect(selectAgentMode('REFACTOR the module', modes)).toBe('praxis')
    expect(selectAgentMode('Migrate to new DB', modes)).toBe('praxis')
  })

  it('returns "praxis" for long goals (>300 chars)', () => {
    const modes = makeModes('praxis')
    const longGoal = 'a'.repeat(301)
    expect(selectAgentMode(longGoal, modes)).toBe('praxis')
  })

  it('returns undefined for simple goals', () => {
    const modes = makeModes('praxis', 'plan')
    expect(selectAgentMode('create hello.ts with console.log', modes)).toBeUndefined()
    expect(selectAgentMode('fix the typo in README', modes)).toBeUndefined()
    expect(selectAgentMode('add a test for utils', modes)).toBeUndefined()
    expect(selectAgentMode('list files in src/', modes)).toBeUndefined()
  })

  it('returns undefined for goals at exactly 300 chars', () => {
    const modes = makeModes('praxis')
    const goal = 'x'.repeat(300)
    expect(selectAgentMode(goal, modes)).toBeUndefined()
  })
})
