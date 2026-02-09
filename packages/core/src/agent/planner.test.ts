/**
 * Agent Planner Tests
 *
 * Tests pure/non-LLM parts of the AgentPlanner:
 * - classifyError: error string -> RecoveryStrategy mapping
 * - Constructor: default and custom config merging
 */

import { describe, expect, it } from 'vitest'
import type { RecoveryStrategy } from './planner.js'
import { AgentPlanner } from './planner.js'

// ============================================================================
// classifyError Tests
// ============================================================================

describe('classifyError', () => {
  const planner = new AgentPlanner()

  // --------------------------------------------------------------------------
  // Permission/access errors -> 'alternate'
  // --------------------------------------------------------------------------

  it('returns alternate for "permission denied"', () => {
    expect(planner.classifyError('permission denied')).toBe('alternate')
  })

  it('returns alternate for "eacces"', () => {
    expect(planner.classifyError('eacces')).toBe('alternate')
  })

  it('returns alternate for "access denied"', () => {
    expect(planner.classifyError('access denied')).toBe('alternate')
  })

  it('returns alternate for mixed case "Permission Denied"', () => {
    expect(planner.classifyError('Permission Denied')).toBe('alternate')
  })

  it('returns alternate for error containing "EACCES" in uppercase', () => {
    expect(planner.classifyError('Error: EACCES: open /etc/shadow')).toBe('alternate')
  })

  // --------------------------------------------------------------------------
  // Not found errors -> 'decompose'
  // --------------------------------------------------------------------------

  it('returns decompose for "not found"', () => {
    expect(planner.classifyError('not found')).toBe('decompose')
  })

  it('returns decompose for "enoent"', () => {
    expect(planner.classifyError('enoent')).toBe('decompose')
  })

  it('returns decompose for "no such file"', () => {
    expect(planner.classifyError('no such file')).toBe('decompose')
  })

  it('returns decompose for "ENOENT: no such file or directory"', () => {
    expect(
      planner.classifyError('ENOENT: no such file or directory, open "/tmp/missing.txt"')
    ).toBe('decompose')
  })

  // --------------------------------------------------------------------------
  // Timeout errors -> 'retry'
  // --------------------------------------------------------------------------

  it('returns retry for "timeout"', () => {
    expect(planner.classifyError('timeout')).toBe('retry')
  })

  it('returns retry for "timed out"', () => {
    expect(planner.classifyError('timed out')).toBe('retry')
  })

  it('returns retry for "Request timed out after 30s"', () => {
    expect(planner.classifyError('Request timed out after 30s')).toBe('retry')
  })

  // --------------------------------------------------------------------------
  // Validation errors -> 'alternate'
  // --------------------------------------------------------------------------

  it('returns alternate for "validation"', () => {
    expect(planner.classifyError('validation')).toBe('alternate')
  })

  it('returns alternate for "invalid"', () => {
    expect(planner.classifyError('invalid')).toBe('alternate')
  })

  it('returns alternate for "Validation failed: missing required field"', () => {
    expect(planner.classifyError('Validation failed: missing required field')).toBe('alternate')
  })

  // --------------------------------------------------------------------------
  // Syntax/parse errors -> 'abort'
  // --------------------------------------------------------------------------

  it('returns abort for "syntax error"', () => {
    expect(planner.classifyError('syntax error')).toBe('abort')
  })

  it('returns abort for "parse error"', () => {
    expect(planner.classifyError('parse error')).toBe('abort')
  })

  // --------------------------------------------------------------------------
  // Connection/network errors -> 'retry'
  // --------------------------------------------------------------------------

  it('returns retry for "connection"', () => {
    expect(planner.classifyError('connection')).toBe('retry')
  })

  it('returns retry for "network"', () => {
    expect(planner.classifyError('network')).toBe('retry')
  })

  it('returns retry for "Connection refused to localhost:5432"', () => {
    expect(planner.classifyError('Connection refused to localhost:5432')).toBe('retry')
  })

  // --------------------------------------------------------------------------
  // Default / unknown errors -> 'retry'
  // --------------------------------------------------------------------------

  it('returns retry for unknown error string', () => {
    expect(planner.classifyError('something unexpected happened')).toBe('retry')
  })

  it('returns retry for empty string', () => {
    expect(planner.classifyError('')).toBe('retry')
  })

  // --------------------------------------------------------------------------
  // Priority / ordering tests
  // --------------------------------------------------------------------------

  it('matches permission before not-found when both present', () => {
    // "permission denied" check comes before "not found" check
    expect(planner.classifyError('permission denied: file not found')).toBe('alternate')
  })

  it('matches not-found before timeout when both present', () => {
    // "not found" check comes before "timeout" check
    expect(planner.classifyError('resource not found, timeout ignored')).toBe('decompose')
  })

  // --------------------------------------------------------------------------
  // Return type validation
  // --------------------------------------------------------------------------

  it('always returns a valid RecoveryStrategy', () => {
    const validStrategies: RecoveryStrategy[] = [
      'retry',
      'alternate',
      'decompose',
      'rollback',
      'skip',
      'abort',
    ]
    const testErrors = [
      'permission denied',
      'eacces',
      'not found',
      'enoent',
      'timeout',
      'timed out',
      'validation',
      'invalid',
      'syntax error',
      'parse error',
      'connection',
      'network',
      'random gibberish',
      '',
    ]

    for (const error of testErrors) {
      const result = planner.classifyError(error)
      expect(validStrategies).toContain(result)
    }
  })
})

// ============================================================================
// Constructor Tests
// ============================================================================

describe('AgentPlanner constructor', () => {
  it('creates instance with default config when no args provided', () => {
    const planner = new AgentPlanner()

    // Verify it is a valid instance by calling classifyError
    expect(planner.classifyError('test')).toBe('retry')
  })

  it('creates instance with empty config object', () => {
    const planner = new AgentPlanner({})

    expect(planner.classifyError('permission denied')).toBe('alternate')
  })

  it('accepts partial config with only provider', () => {
    const planner = new AgentPlanner({ provider: 'openai' })

    // Instance should work - classifyError does not depend on config
    expect(planner.classifyError('timeout')).toBe('retry')
  })

  it('accepts partial config with only model', () => {
    const planner = new AgentPlanner({ model: 'gpt-4o' })

    expect(planner.classifyError('syntax error')).toBe('abort')
  })

  it('accepts partial config with only maxSteps', () => {
    const planner = new AgentPlanner({ maxSteps: 5 })

    expect(planner.classifyError('network')).toBe('retry')
  })

  it('accepts full config', () => {
    const planner = new AgentPlanner({
      provider: 'openrouter',
      model: 'anthropic/claude-sonnet-4-20250514',
      maxSteps: 20,
    })

    expect(planner.classifyError('enoent')).toBe('decompose')
  })

  it('does not throw with undefined config', () => {
    expect(() => new AgentPlanner(undefined)).not.toThrow()
  })
})
