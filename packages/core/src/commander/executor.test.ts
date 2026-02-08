/**
 * Executor Tests
 *
 * Tests for pure helper functions: getFilteredTools, isDelegationTool
 * (executeWorker integration requires AgentExecutor/LLM so is not tested here)
 */

import { describe, expect, it } from 'vitest'
import { DELEGATE_TOOL_PREFIX, getFilteredTools, isDelegationTool } from './executor.js'

// ============================================================================
// getFilteredTools
// ============================================================================

describe('getFilteredTools', () => {
  it('passes through regular tools', () => {
    const tools = ['read', 'write', 'grep', 'glob', 'bash']
    expect(getFilteredTools(tools)).toEqual(tools)
  })

  it('filters out delegate_* tools', () => {
    const tools = ['read', 'delegate_coder', 'write', 'delegate_tester']
    const filtered = getFilteredTools(tools)

    expect(filtered).toEqual(['read', 'write'])
  })

  it('filters all delegate tools when only delegates present', () => {
    const tools = ['delegate_coder', 'delegate_tester', 'delegate_reviewer']
    expect(getFilteredTools(tools)).toEqual([])
  })

  it('handles empty list', () => {
    expect(getFilteredTools([])).toEqual([])
  })

  it('is case-sensitive', () => {
    const tools = ['Delegate_coder', 'DELEGATE_coder', 'delegate_coder']
    const filtered = getFilteredTools(tools)

    // Only lowercase delegate_ is filtered
    expect(filtered).toEqual(['Delegate_coder', 'DELEGATE_coder'])
  })
})

// ============================================================================
// isDelegationTool
// ============================================================================

describe('isDelegationTool', () => {
  it('returns true for delegate_ tools', () => {
    expect(isDelegationTool('delegate_coder')).toBe(true)
    expect(isDelegationTool('delegate_tester')).toBe(true)
    expect(isDelegationTool('delegate_reviewer')).toBe(true)
  })

  it('returns false for regular tools', () => {
    expect(isDelegationTool('read')).toBe(false)
    expect(isDelegationTool('write')).toBe(false)
    expect(isDelegationTool('bash')).toBe(false)
  })

  it('returns false for similar but not matching names', () => {
    expect(isDelegationTool('delegated_coder')).toBe(false)
    expect(isDelegationTool('Delegate_coder')).toBe(false)
    expect(isDelegationTool('delegate')).toBe(false)
  })
})

// ============================================================================
// Constants
// ============================================================================

describe('DELEGATE_TOOL_PREFIX', () => {
  it('equals "delegate_"', () => {
    expect(DELEGATE_TOOL_PREFIX).toBe('delegate_')
  })
})
