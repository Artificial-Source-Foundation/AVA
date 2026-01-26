/**
 * Operator Guard Tests
 *
 * Uses representative sampling for operator detection and tool blocking.
 */

import { describe, it, expect } from 'vitest'
import {
  checkOperatorGuard,
  formatOperatorViolation,
  isOperatorAgent,
  getOperatorBlockedTools,
  getOperatorPatterns,
} from '../../src/guards/operator-guard.js'

describe('isOperatorAgent', () => {
  it('should identify operator variants correctly', () => {
    // Positive cases - representative operator patterns
    expect(isOperatorAgent('operator')).toBe(true)
    expect(isOperatorAgent('Operator')).toBe(true) // case insensitive
    expect(isOperatorAgent('operator_complex')).toBe(true)
    expect(isOperatorAgent('ui_ops')).toBe(true)
    expect(isOperatorAgent('scribe')).toBe(true)

    // Negative cases - non-operators
    expect(isOperatorAgent('commander')).toBe(false)
    expect(isOperatorAgent('validator')).toBe(false)
    expect(isOperatorAgent('scout')).toBe(false)
  })
})

describe('checkOperatorGuard', () => {
  it('should block orchestration tools for operators', () => {
    // Representative blocked tools
    const blockedTools = ['delegate_task', 'mission_create', 'consult_council']
    for (const tool of blockedTools) {
      const result = checkOperatorGuard({ agent: 'operator', toolName: tool, toolArgs: {} })
      expect(result.blocked).toBe(true)
      expect(result.reason).toContain('Operator cannot use')
      expect(result.suggestion).toBeDefined()
    }
  })

  it('should allow execution tools for operators', () => {
    const allowedTools = ['Edit', 'Write', 'Read', 'Bash', 'task_complete']
    for (const tool of allowedTools) {
      expect(checkOperatorGuard({ agent: 'operator', toolName: tool, toolArgs: {} }).blocked).toBe(false)
    }
  })

  it('should not block tools for non-operator agents', () => {
    // Commander and others can use delegation tools
    expect(checkOperatorGuard({ agent: 'commander', toolName: 'delegate_task', toolArgs: {} }).blocked).toBe(false)
    expect(checkOperatorGuard({ agent: 'validator', toolName: 'delegate_task', toolArgs: {} }).blocked).toBe(false)
  })

  it('should block delegation for all operator variants', () => {
    const variants = ['operator', 'operator_complex', 'ui_ops', 'scribe']
    for (const agent of variants) {
      expect(checkOperatorGuard({ agent, toolName: 'delegate_task', toolArgs: {} }).blocked).toBe(true)
    }
  })
})

describe('formatOperatorViolation', () => {
  it('should format violation messages correctly', () => {
    expect(formatOperatorViolation({ blocked: false })).toBe('')

    const result = formatOperatorViolation({
      blocked: true,
      reason: 'Operator cannot use delegate_task',
      suggestion: 'Use task_complete instead',
    })
    expect(result).toContain('[OPERATOR GUARD VIOLATION]')
    expect(result).toContain('delegate_task')
    expect(result).toContain('task_complete')
  })
})

describe('helper functions', () => {
  it('should return blocked tools and patterns', () => {
    const tools = getOperatorBlockedTools()
    expect(tools).toContain('delegate_task')
    expect(tools).toContain('mission_create')

    const patterns = getOperatorPatterns()
    expect(patterns).toContain('operator')
    expect(patterns).toContain('scribe')
  })
})
