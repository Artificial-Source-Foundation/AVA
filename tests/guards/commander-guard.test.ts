/**
 * Commander Guard Tests
 *
 * Uses representative sampling - if Edit is blocked, Write/MultiEdit follow same pattern.
 */

import { describe, it, expect } from 'vitest'
import { checkCommanderGuard, formatGuardViolation } from '../../src/guards/commander-guard.js'

describe('checkCommanderGuard', () => {
  it('should allow any tool for non-commander agents', () => {
    // Operators can use anything
    expect(checkCommanderGuard({ agent: 'operator', toolName: 'Edit', toolArgs: {} }).blocked).toBe(false)
    // Validators can use anything
    expect(checkCommanderGuard({ agent: 'validator', toolName: 'bash', toolArgs: { command: 'npm test' } }).blocked).toBe(false)
    // Case insensitive
    expect(checkCommanderGuard({ agent: 'COMMANDER', toolName: 'Read', toolArgs: {} }).blocked).toBe(false)
  })

  it('should block file modification tools for commander', () => {
    // Test representative tools - Edit and MultiEdit cover the pattern
    const editResult = checkCommanderGuard({ agent: 'commander', toolName: 'Edit', toolArgs: {} })
    expect(editResult.blocked).toBe(true)
    expect(editResult.reason).toContain('Commander cannot use')
    expect(editResult.suggestion).toContain('dispatch_task')

    const multiEditResult = checkCommanderGuard({ agent: 'commander', toolName: 'MultiEdit', toolArgs: {} })
    expect(multiEditResult.blocked).toBe(true)
  })

  it('should block test/build bash commands for commander', () => {
    // Representative test commands
    const testCmds = ['npm test', 'vitest', 'jest']
    for (const cmd of testCmds) {
      const result = checkCommanderGuard({ agent: 'commander', toolName: 'bash', toolArgs: { command: cmd } })
      expect(result.blocked).toBe(true)
      expect(result.reason).toContain('Commander cannot run')
    }
  })

  it('should allow read-only bash commands for commander', () => {
    // Representative allowed commands
    const allowedCmds = ['ls -la', 'git status', 'npm list']
    for (const cmd of allowedCmds) {
      expect(checkCommanderGuard({ agent: 'commander', toolName: 'bash', toolArgs: { command: cmd } }).blocked).toBe(false)
    }
  })

  it('should allow orchestration and read tools for commander', () => {
    const allowedTools = ['Read', 'Glob', 'dispatch_task', 'consult_council']
    for (const tool of allowedTools) {
      expect(checkCommanderGuard({ agent: 'commander', toolName: tool, toolArgs: {} }).blocked).toBe(false)
    }
  })
})

describe('formatGuardViolation', () => {
  it('should format violation messages correctly', () => {
    // Non-blocked returns empty
    expect(formatGuardViolation({ blocked: false })).toBe('')

    // Blocked includes all parts
    const result = formatGuardViolation({
      blocked: true,
      reason: 'Commander cannot use Edit',
      suggestion: 'Use dispatch_task',
    })
    expect(result).toContain('[COMMANDER GUARD VIOLATION]')
    expect(result).toContain('Commander cannot use Edit')
    expect(result).toContain('dispatch_task')
  })
})
