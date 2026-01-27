/**
 * Tests for Delta9 Compliance Hooks
 */

import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import {
  checkCompliance,
  getComplianceReminder,
  checkAndTrack,
  registerRule,
  unregisterRule,
  enableRule,
  disableRule,
  getRules,
  clearRules,
  registerDefaultRules,
  trackToolExecution,
  getRecentTools,
  clearToolHistory,
  clearAllToolHistory,
  createComplianceContext,
  isCodeReadTool,
  isCodeModifyTool,
  isDelegationTool,
  isValidationTool,
  isTaskCompletionTool,
  type AgentRole,
  type ComplianceRule,
} from '../../src/lib/compliance-hooks.js'

describe('tool category helpers', () => {
  describe('isCodeReadTool', () => {
    it('identifies code read tools', () => {
      expect(isCodeReadTool('read_file')).toBe(true)
      expect(isCodeReadTool('grep')).toBe(true)
      expect(isCodeReadTool('glob')).toBe(true)
      expect(isCodeReadTool('list_files')).toBe(true)
      expect(isCodeReadTool('search_code')).toBe(true)
      expect(isCodeReadTool('view_file')).toBe(true)
    })

    it('rejects non-read tools', () => {
      expect(isCodeReadTool('write_file')).toBe(false)
      expect(isCodeReadTool('bash')).toBe(false)
      expect(isCodeReadTool('dispatch_task')).toBe(false)
    })

    it('is case insensitive', () => {
      expect(isCodeReadTool('READ_FILE')).toBe(true)
      expect(isCodeReadTool('Grep')).toBe(true)
    })
  })

  describe('isCodeModifyTool', () => {
    it('identifies code modify tools', () => {
      expect(isCodeModifyTool('write_file')).toBe(true)
      expect(isCodeModifyTool('edit_file')).toBe(true)
      expect(isCodeModifyTool('replace_in_file')).toBe(true)
      expect(isCodeModifyTool('create_file')).toBe(true)
      expect(isCodeModifyTool('delete_file')).toBe(true)
      expect(isCodeModifyTool('apply_patch')).toBe(true)
    })

    it('rejects non-modify tools', () => {
      expect(isCodeModifyTool('read_file')).toBe(false)
      expect(isCodeModifyTool('bash')).toBe(false)
    })
  })

  describe('isDelegationTool', () => {
    it('identifies delegation tools', () => {
      expect(isDelegationTool('dispatch_task')).toBe(true)
      expect(isDelegationTool('delegate_task')).toBe(true)
      expect(isDelegationTool('spawn_operator')).toBe(true)
      expect(isDelegationTool('launch_squadron')).toBe(true)
    })

    it('rejects non-delegation tools', () => {
      expect(isDelegationTool('read_file')).toBe(false)
      expect(isDelegationTool('write_file')).toBe(false)
    })
  })

  describe('isValidationTool', () => {
    it('identifies validation tools', () => {
      expect(isValidationTool('validation_result')).toBe(true)
      expect(isValidationTool('report_validation')).toBe(true)
      expect(isValidationTool('task_validate')).toBe(true)
    })

    it('rejects non-validation tools', () => {
      expect(isValidationTool('task_complete')).toBe(false)
      expect(isValidationTool('read_file')).toBe(false)
    })
  })

  describe('isTaskCompletionTool', () => {
    it('identifies task completion tools', () => {
      expect(isTaskCompletionTool('task_complete')).toBe(true)
      expect(isTaskCompletionTool('complete_task')).toBe(true)
      expect(isTaskCompletionTool('task_done')).toBe(true)
      expect(isTaskCompletionTool('report_completion')).toBe(true)
    })

    it('rejects non-completion tools', () => {
      expect(isTaskCompletionTool('validation_result')).toBe(false)
      expect(isTaskCompletionTool('read_file')).toBe(false)
    })
  })
})

describe('rule management', () => {
  beforeEach(() => {
    clearRules()
  })

  afterEach(() => {
    clearRules()
  })

  it('registers a custom rule', () => {
    const rule: ComplianceRule = {
      name: 'custom-rule',
      description: 'Test rule',
      roles: ['operator'],
      severity: 'warning',
      enabled: true,
      check: () => null,
    }

    registerRule(rule)

    const rules = getRules()
    expect(rules).toHaveLength(1)
    expect(rules[0].name).toBe('custom-rule')
  })

  it('unregisters a rule', () => {
    registerRule({
      name: 'to-remove',
      description: 'Remove me',
      roles: ['operator'],
      severity: 'info',
      enabled: true,
      check: () => null,
    })

    expect(unregisterRule('to-remove')).toBe(true)
    expect(getRules()).toHaveLength(0)
  })

  it('returns false when unregistering non-existent rule', () => {
    expect(unregisterRule('non-existent')).toBe(false)
  })

  it('enables a rule', () => {
    registerRule({
      name: 'toggle-rule',
      description: 'Toggle me',
      roles: ['operator'],
      severity: 'info',
      enabled: false,
      check: () => null,
    })

    enableRule('toggle-rule')

    const rules = getRules()
    expect(rules[0].enabled).toBe(true)
  })

  it('disables a rule', () => {
    registerRule({
      name: 'disable-rule',
      description: 'Disable me',
      roles: ['operator'],
      severity: 'info',
      enabled: true,
      check: () => null,
    })

    disableRule('disable-rule')

    const rules = getRules()
    expect(rules[0].enabled).toBe(false)
  })

  it('clears all rules', () => {
    registerRule({
      name: 'rule1',
      description: 'Rule 1',
      roles: ['operator'],
      severity: 'info',
      enabled: true,
      check: () => null,
    })
    registerRule({
      name: 'rule2',
      description: 'Rule 2',
      roles: ['operator'],
      severity: 'info',
      enabled: true,
      check: () => null,
    })

    clearRules()

    expect(getRules()).toHaveLength(0)
  })
})

describe('tool history tracking', () => {
  const sessionId = 'test-session'

  beforeEach(() => {
    clearAllToolHistory()
  })

  afterEach(() => {
    clearAllToolHistory()
  })

  it('tracks tool executions', () => {
    trackToolExecution(sessionId, 'read_file')
    trackToolExecution(sessionId, 'grep')

    const history = getRecentTools(sessionId)

    expect(history).toEqual(['read_file', 'grep'])
  })

  it('limits history to 10 entries', () => {
    for (let i = 0; i < 15; i++) {
      trackToolExecution(sessionId, `tool-${i}`)
    }

    const history = getRecentTools(sessionId)

    expect(history).toHaveLength(10)
    expect(history[0]).toBe('tool-5')
    expect(history[9]).toBe('tool-14')
  })

  it('returns empty array for unknown session', () => {
    const history = getRecentTools('unknown-session')
    expect(history).toEqual([])
  })

  it('clears history for specific session', () => {
    trackToolExecution('session-1', 'tool-a')
    trackToolExecution('session-2', 'tool-b')

    clearToolHistory('session-1')

    expect(getRecentTools('session-1')).toEqual([])
    expect(getRecentTools('session-2')).toEqual(['tool-b'])
  })

  it('clears all history', () => {
    trackToolExecution('session-1', 'tool-a')
    trackToolExecution('session-2', 'tool-b')

    clearAllToolHistory()

    expect(getRecentTools('session-1')).toEqual([])
    expect(getRecentTools('session-2')).toEqual([])
  })
})

describe('createComplianceContext', () => {
  beforeEach(() => {
    clearAllToolHistory()
  })

  afterEach(() => {
    clearAllToolHistory()
  })

  it('creates context with tool history', () => {
    trackToolExecution('ctx-session', 'read_file')
    trackToolExecution('ctx-session', 'grep')

    const context = createComplianceContext('ctx-session', 'write_file', 'operator')

    expect(context.sessionId).toBe('ctx-session')
    expect(context.toolName).toBe('write_file')
    expect(context.role).toBe('operator')
    expect(context.recentTools).toEqual(['read_file', 'grep'])
  })

  it('creates context without role', () => {
    const context = createComplianceContext('session', 'tool')

    expect(context.role).toBeUndefined()
  })
})

describe('compliance checking', () => {
  beforeEach(() => {
    clearRules()
    clearAllToolHistory()
  })

  afterEach(() => {
    clearRules()
    clearAllToolHistory()
  })

  it('returns no violation when no rules', () => {
    const result = checkCompliance({
      sessionId: 'test',
      toolName: 'read_file',
      role: 'commander',
    })

    expect(result.hasViolation).toBe(false)
    expect(result.reminder).toBeNull()
  })

  it('detects violation from custom rule', () => {
    registerRule({
      name: 'test-violation',
      description: 'Always violates',
      roles: ['operator'],
      severity: 'warning',
      enabled: true,
      check: () => ({
        hasViolation: true,
        severity: 'warning',
        reminder: 'Test violation detected',
        rule: 'test-violation',
      }),
    })

    const result = checkCompliance({
      sessionId: 'test',
      toolName: 'any',
      role: 'operator',
    })

    expect(result.hasViolation).toBe(true)
    expect(result.reminder).toBe('Test violation detected')
  })

  it('skips disabled rules', () => {
    registerRule({
      name: 'disabled-rule',
      description: 'Disabled',
      roles: ['operator'],
      severity: 'error',
      enabled: false,
      check: () => ({
        hasViolation: true,
        severity: 'error',
        reminder: 'Should not see this',
      }),
    })

    const result = checkCompliance({
      sessionId: 'test',
      toolName: 'any',
      role: 'operator',
    })

    expect(result.hasViolation).toBe(false)
  })

  it('skips rules for non-matching roles', () => {
    registerRule({
      name: 'commander-only',
      description: 'Commander only',
      roles: ['commander'],
      severity: 'warning',
      enabled: true,
      check: () => ({
        hasViolation: true,
        severity: 'warning',
        reminder: 'Commander violation',
      }),
    })

    const result = checkCompliance({
      sessionId: 'test',
      toolName: 'any',
      role: 'operator',
    })

    expect(result.hasViolation).toBe(false)
  })

  it('applies rules with unknown role', () => {
    registerRule({
      name: 'unknown-rule',
      description: 'Applies to unknown',
      roles: ['unknown'],
      severity: 'info',
      enabled: true,
      check: () => ({
        hasViolation: true,
        severity: 'info',
        reminder: 'Unknown role reminder',
      }),
    })

    const result = checkCompliance({
      sessionId: 'test',
      toolName: 'any',
      // No role specified
    })

    expect(result.hasViolation).toBe(true)
  })
})

describe('getComplianceReminder', () => {
  beforeEach(() => {
    clearRules()
  })

  afterEach(() => {
    clearRules()
  })

  it('returns reminder string when violation', () => {
    registerRule({
      name: 'reminder-rule',
      description: 'Has reminder',
      roles: ['operator'],
      severity: 'info',
      enabled: true,
      check: () => ({
        hasViolation: true,
        severity: 'info',
        reminder: 'This is a reminder',
      }),
    })

    const reminder = getComplianceReminder({
      sessionId: 'test',
      toolName: 'any',
      role: 'operator',
    })

    expect(reminder).toBe('This is a reminder')
  })

  it('returns null when no violation', () => {
    const reminder = getComplianceReminder({
      sessionId: 'test',
      toolName: 'any',
      role: 'operator',
    })

    expect(reminder).toBeNull()
  })
})

describe('checkAndTrack', () => {
  beforeEach(() => {
    clearRules()
    clearAllToolHistory()
  })

  afterEach(() => {
    clearRules()
    clearAllToolHistory()
  })

  it('tracks tool and checks compliance', () => {
    registerRule({
      name: 'track-rule',
      description: 'Checks history',
      roles: ['operator'],
      severity: 'info',
      enabled: true,
      check: (ctx) => {
        if (ctx.recentTools && ctx.recentTools.length > 2) {
          return {
            hasViolation: true,
            severity: 'info',
            reminder: 'Too many tools',
          }
        }
        return null
      },
    })

    // First calls - no violation
    checkAndTrack('session', 'tool1', 'operator')
    checkAndTrack('session', 'tool2', 'operator')
    const result3 = checkAndTrack('session', 'tool3', 'operator')

    // Third call should trigger (recent tools = tool1, tool2, now adding tool3)
    expect(result3.hasViolation).toBe(true)

    // Verify tracking
    expect(getRecentTools('session')).toEqual(['tool1', 'tool2', 'tool3'])
  })
})

describe('default rules', () => {
  beforeEach(() => {
    clearRules()
    clearAllToolHistory()
    registerDefaultRules()
  })

  afterEach(() => {
    clearRules()
    clearAllToolHistory()
  })

  it('registers 5 default rules', () => {
    expect(getRules()).toHaveLength(5)
  })

  describe('commander-no-code-read', () => {
    it('triggers when commander reads code', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'read_file',
        role: 'commander',
      })

      expect(result.hasViolation).toBe(true)
      expect(result.severity).toBe('warning')
      expect(result.reminder).toContain('Commander reads but does not implement')
    })

    it('does not trigger for operators', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'read_file',
        role: 'operator',
      })

      expect(result.hasViolation).toBe(false)
    })
  })

  describe('commander-no-code-modify', () => {
    it('triggers when commander modifies code', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'write_file',
        role: 'commander',
      })

      expect(result.hasViolation).toBe(true)
      expect(result.severity).toBe('error')
      expect(result.reminder).toContain('Commander NEVER writes code')
    })

    it('does not trigger for operators', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'write_file',
        role: 'operator',
      })

      expect(result.hasViolation).toBe(false)
    })
  })

  describe('operator-validate-after-complete', () => {
    it('triggers when operator completes without validation', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'task_complete',
        role: 'operator',
        recentTools: ['read_file', 'write_file'],
      })

      expect(result.hasViolation).toBe(true)
      expect(result.severity).toBe('info')
      expect(result.reminder).toContain('not validated')
    })

    it('does not trigger when validation was run', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'task_complete',
        role: 'operator',
        recentTools: ['read_file', 'write_file', 'validation_result'],
      })

      expect(result.hasViolation).toBe(false)
    })

    it('does not trigger for non-completion tools', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'write_file',
        role: 'operator',
        recentTools: [],
      })

      expect(result.hasViolation).toBe(false)
    })
  })

  describe('commander-delegate-after-read', () => {
    it('triggers when commander reads but does not delegate', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'think', // Some non-read, non-delegation tool
        role: 'commander',
        recentTools: ['read_file', 'grep', 'list_files'],
      })

      expect(result.hasViolation).toBe(true)
      expect(result.severity).toBe('info')
      expect(result.reminder).toContain('Commander has been reading code')
    })

    it('does not trigger when delegation was done', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'think',
        role: 'commander',
        recentTools: ['read_file', 'dispatch_task', 'list_files'],
      })

      expect(result.hasViolation).toBe(false)
    })

    it('does not trigger with insufficient history', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'think',
        role: 'commander',
        recentTools: ['read_file'],
      })

      expect(result.hasViolation).toBe(false)
    })

    it('does not trigger during read operations', () => {
      // When the current tool is a read tool, don't spam
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'read_file',
        role: 'commander',
        recentTools: ['read_file', 'grep', 'list_files'],
      })

      // Should only trigger commander-no-code-read, not this rule
      expect(result.rule).toBe('commander-no-code-read')
    })
  })

  describe('commander-delegate-recon (BUG-10)', () => {
    it('triggers when commander uses multiple exploration tools', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'glob',
        role: 'commander',
        recentTools: ['glob', 'list_files'],
      })

      expect(result.hasViolation).toBe(true)
      expect(result.rule).toBe('commander-delegate-recon')
      expect(result.reminder).toContain('RECON')
      expect(result.suggestion).toContain('scout')
    })

    it('triggers when commander reads many files', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'read_file',
        role: 'commander',
        recentTools: ['read_file', 'read_file', 'read_file'],
      })

      expect(result.hasViolation).toBe(true)
      expect(result.rule).toBe('commander-delegate-recon')
    })

    it('does not trigger with delegation present', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'glob',
        role: 'commander',
        recentTools: ['glob', 'delegate_task', 'list_files'],
      })

      // Should not trigger recon rule when delegation is present
      expect(result.rule).not.toBe('commander-delegate-recon')
    })

    it('does not trigger for operators', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'glob',
        role: 'operator',
        recentTools: ['glob', 'list_files'],
      })

      expect(result.rule).not.toBe('commander-delegate-recon')
    })

    it('does not trigger with insufficient exploration', () => {
      const result = checkCompliance({
        sessionId: 'test',
        toolName: 'glob',
        role: 'commander',
        recentTools: ['glob'],
      })

      // Only 1 exploration tool, needs 2+
      expect(result.rule).not.toBe('commander-delegate-recon')
    })
  })
})
