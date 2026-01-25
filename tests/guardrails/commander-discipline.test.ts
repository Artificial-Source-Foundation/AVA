/**
 * Tests for Commander Discipline
 */

import { describe, it, expect, beforeEach } from 'vitest'
import {
  CommanderDisciplineEnforcer,
  isToolAllowed,
  checkToolUse,
  checkResponseForCode,
  clearViolations,
  getViolations,
  getRecentViolations,
  resetDisciplineEnforcer,
} from '../../src/guardrails/commander-discipline.js'
import {
  COMMANDER_ALLOWED_TOOLS,
  COMMANDER_PROHIBITED_TOOLS,
} from '../../src/guardrails/types.js'

describe('Commander Discipline', () => {
  beforeEach(() => {
    clearViolations()
    resetDisciplineEnforcer()
  })

  describe('isToolAllowed', () => {
    it('should allow read-only tools', () => {
      expect(isToolAllowed('Read')).toBe(true)
      expect(isToolAllowed('Glob')).toBe(true)
      expect(isToolAllowed('Grep')).toBe(true)
    })

    it('should allow mission tools', () => {
      expect(isToolAllowed('mission_create')).toBe(true)
      expect(isToolAllowed('mission_status')).toBe(true)
      expect(isToolAllowed('mission_update')).toBe(true)
    })

    it('should allow delegation tools', () => {
      expect(isToolAllowed('delegate_task')).toBe(true)
      expect(isToolAllowed('dispatch_task')).toBe(true)
    })

    it('should allow council tools', () => {
      expect(isToolAllowed('consult_council')).toBe(true)
      expect(isToolAllowed('council_status')).toBe(true)
    })

    it('should prohibit code-writing tools', () => {
      expect(isToolAllowed('Write')).toBe(false)
      expect(isToolAllowed('Edit')).toBe(false)
      expect(isToolAllowed('Bash')).toBe(false)
    })

    it('should handle custom allowed tools', () => {
      expect(isToolAllowed('custom_tool', { strictMode: true, logViolations: false })).toBe(false)
      expect(
        isToolAllowed('custom_tool', {
          strictMode: true,
          logViolations: false,
          additionalAllowedTools: ['custom_tool'],
        })
      ).toBe(true)
    })
  })

  describe('checkToolUse', () => {
    it('should allow non-commander agents to use any tool', () => {
      const result = checkToolUse('Write', 'operator')
      expect(result.allowed).toBe(true)
      expect(result.violation).toBeUndefined()
    })

    it('should block commander from using prohibited tools', () => {
      const result = checkToolUse('Write', 'commander')
      expect(result.allowed).toBe(false)
      expect(result.violation).toBeDefined()
      expect(result.violation!.type).toBe('tool_use')
      expect(result.violation!.tool).toBe('Write')
    })

    it('should provide suggestion for prohibited tools', () => {
      const result = checkToolUse('Write', 'commander')
      expect(result.suggestion).toContain('delegate_task')
    })

    it('should record violations', () => {
      checkToolUse('Write', 'commander')
      checkToolUse('Edit', 'commander')

      const violations = getViolations()
      expect(violations).toHaveLength(2)
    })

    it('should allow commander to use allowed tools', () => {
      const result = checkToolUse('delegate_task', 'commander')
      expect(result.allowed).toBe(true)
    })
  })

  describe('checkResponseForCode', () => {
    it('should allow non-commander responses with code', () => {
      const result = checkResponseForCode('```typescript\nconst x = 1;\n```', 'operator')
      expect(result.allowed).toBe(true)
    })

    it('should detect code blocks in commander responses', () => {
      const config = { strictMode: true, logViolations: false }
      const result = checkResponseForCode('```typescript\nconst x = 1;\n```', 'commander', config)
      expect(result.allowed).toBe(false)
      expect(result.violation!.type).toBe('code_block')
    })

    it('should detect JavaScript code blocks', () => {
      const config = { strictMode: true, logViolations: false }
      const result = checkResponseForCode('```javascript\nfunction test() {}\n```', 'commander', config)
      expect(result.allowed).toBe(false)
    })

    it('should allow non-strict mode with warning', () => {
      const config = { strictMode: false, logViolations: false }
      const result = checkResponseForCode('```typescript\nconst x = 1;\n```', 'commander', config)
      expect(result.allowed).toBe(true) // Allowed but with violation
      expect(result.violation).toBeDefined()
    })

    it('should allow plain text responses', () => {
      const result = checkResponseForCode('I will delegate this task to an Operator.', 'commander')
      expect(result.allowed).toBe(true)
      expect(result.violation).toBeUndefined()
    })

    it('should detect Write tool patterns', () => {
      const config = { strictMode: true, logViolations: false }
      const result = checkResponseForCode('I will use Write( to create the file', 'commander', config)
      expect(result.allowed).toBe(false)
    })
  })

  describe('getViolations', () => {
    it('should return all violations', () => {
      checkToolUse('Write', 'commander')
      checkToolUse('Edit', 'commander')
      checkToolUse('Bash', 'commander')

      const violations = getViolations()
      expect(violations).toHaveLength(3)
    })

    it('should return empty array when no violations', () => {
      const violations = getViolations()
      expect(violations).toEqual([])
    })
  })

  describe('getRecentViolations', () => {
    it('should return limited number of violations', () => {
      for (let i = 0; i < 20; i++) {
        checkToolUse('Write', 'commander')
      }

      const recent = getRecentViolations(5)
      expect(recent).toHaveLength(5)
    })

    it('should return all if less than limit', () => {
      checkToolUse('Write', 'commander')
      checkToolUse('Edit', 'commander')

      const recent = getRecentViolations(10)
      expect(recent).toHaveLength(2)
    })
  })

  describe('CommanderDisciplineEnforcer', () => {
    it('should check tools via checkTool', () => {
      const enforcer = new CommanderDisciplineEnforcer()

      expect(enforcer.checkTool('Read').allowed).toBe(true)
      expect(enforcer.checkTool('Write').allowed).toBe(false)
    })

    it('should check responses via checkResponse', () => {
      const enforcer = new CommanderDisciplineEnforcer({ strictMode: true })

      const result = enforcer.checkResponse('```typescript\ncode\n```')
      expect(result.violation).toBeDefined()
    })

    it('should return allowed tools list', () => {
      const enforcer = new CommanderDisciplineEnforcer()
      const allowed = enforcer.getAllowedTools()

      expect(allowed).toContain('Read')
      expect(allowed).toContain('delegate_task')
    })

    it('should return prohibited tools list', () => {
      const enforcer = new CommanderDisciplineEnforcer()
      const prohibited = enforcer.getProhibitedTools()

      expect(prohibited).toContain('Write')
      expect(prohibited).toContain('Edit')
    })

    it('should track violation count', () => {
      clearViolations()
      const enforcer = new CommanderDisciplineEnforcer()

      enforcer.checkTool('Write')
      enforcer.checkTool('Edit')

      expect(enforcer.getViolationCount()).toBe(2)
    })

    it('should clear violations', () => {
      const enforcer = new CommanderDisciplineEnforcer()

      enforcer.checkTool('Write')
      expect(enforcer.getViolationCount()).toBeGreaterThan(0)

      enforcer.clearViolations()
      expect(enforcer.getViolationCount()).toBe(0)
    })
  })

  describe('Tool Lists', () => {
    it('should have non-overlapping allowed and prohibited lists', () => {
      const allowed = new Set(COMMANDER_ALLOWED_TOOLS)
      const prohibited = new Set(COMMANDER_PROHIBITED_TOOLS)

      for (const tool of allowed) {
        expect(prohibited.has(tool as never)).toBe(false)
      }
    })

    it('should have all planning tools allowed', () => {
      const planningTools = ['mission_create', 'mission_status', 'mission_update', 'mission_add_objective', 'mission_add_task']

      for (const tool of planningTools) {
        expect(COMMANDER_ALLOWED_TOOLS.includes(tool as never)).toBe(true)
      }
    })

    it('should have all code-writing tools prohibited', () => {
      expect(COMMANDER_PROHIBITED_TOOLS.includes('Write')).toBe(true)
      expect(COMMANDER_PROHIBITED_TOOLS.includes('Edit')).toBe(true)
    })
  })
})
