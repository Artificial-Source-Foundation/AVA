/**
 * Policy Engine Tests
 */

import { beforeEach, describe, expect, it } from 'vitest'
import { PolicyEngine } from './engine.js'
import { ApprovalMode } from './rules.js'

describe('PolicyEngine', () => {
  let engine: PolicyEngine

  beforeEach(() => {
    engine = new PolicyEngine()
  })

  // =========================================================================
  // Basic Rule Evaluation
  // =========================================================================

  describe('basic evaluation', () => {
    it('should allow read tools by default', async () => {
      const result = await engine.check('read_file', { path: '/foo/bar.ts' })
      expect(result.decision).toBe('allow')
    })

    it('should allow glob by default', async () => {
      const result = await engine.check('glob', { pattern: '*.ts' })
      expect(result.decision).toBe('allow')
    })

    it('should ask for write tools by default', async () => {
      const result = await engine.check('write_file', { path: '/foo/bar.ts', content: 'hello' })
      expect(result.decision).toBe('ask_user')
    })

    it('should ask for bash by default', async () => {
      const result = await engine.check('bash', { command: 'echo hello' })
      expect(result.decision).toBe('ask_user')
    })

    it('should ask for delete by default', async () => {
      const result = await engine.check('delete_file', { path: '/foo/bar.ts' })
      expect(result.decision).toBe('ask_user')
    })

    it('should ask for unknown tools (fallback)', async () => {
      const result = await engine.check('unknown_tool', { foo: 'bar' })
      expect(result.decision).toBe('ask_user')
    })
  })

  // =========================================================================
  // Plan Mode
  // =========================================================================

  describe('plan mode', () => {
    beforeEach(() => {
      engine.setApprovalMode(ApprovalMode.PLAN)
    })

    it('should allow read_file in plan mode', async () => {
      const result = await engine.check('read_file', { path: '/foo.ts' })
      expect(result.decision).toBe('allow')
    })

    it('should allow grep in plan mode', async () => {
      const result = await engine.check('grep', { pattern: 'foo' })
      expect(result.decision).toBe('allow')
    })

    it('should allow ls in plan mode', async () => {
      const result = await engine.check('ls', { path: '.' })
      expect(result.decision).toBe('allow')
    })

    it('should deny write in plan mode', async () => {
      const result = await engine.check('write_file', { path: '/foo.ts', content: 'x' })
      expect(result.decision).toBe('deny')
      expect(result.denyMessage).toContain('plan mode')
    })

    it('should deny bash in plan mode', async () => {
      const result = await engine.check('bash', { command: 'rm -rf /' })
      expect(result.decision).toBe('deny')
    })

    it('should deny edit in plan mode', async () => {
      const result = await engine.check('edit', { path: '/foo.ts' })
      expect(result.decision).toBe('deny')
    })

    it('should allow websearch in plan mode', async () => {
      const result = await engine.check('websearch', { query: 'test' })
      expect(result.decision).toBe('allow')
    })
  })

  // =========================================================================
  // YOLO Mode
  // =========================================================================

  describe('yolo mode', () => {
    beforeEach(() => {
      engine.setApprovalMode(ApprovalMode.YOLO)
    })

    it('should allow everything in yolo mode', async () => {
      const result = await engine.check('bash', { command: 'rm -rf /' })
      expect(result.decision).toBe('allow')
    })

    it('should allow write in yolo mode', async () => {
      const result = await engine.check('write_file', { path: '/etc/hosts', content: '' })
      expect(result.decision).toBe('allow')
    })

    it('should allow delete in yolo mode', async () => {
      const result = await engine.check('delete_file', { path: '/foo.ts' })
      expect(result.decision).toBe('allow')
    })
  })

  // =========================================================================
  // Auto-Edit Mode
  // =========================================================================

  describe('auto-edit mode', () => {
    beforeEach(() => {
      engine.setApprovalMode(ApprovalMode.AUTO_EDIT)
    })

    it('should allow write_file in auto-edit mode', async () => {
      const result = await engine.check('write_file', { path: '/foo.ts', content: '' })
      expect(result.decision).toBe('allow')
    })

    it('should allow edit in auto-edit mode', async () => {
      const result = await engine.check('edit', { path: '/foo.ts' })
      expect(result.decision).toBe('allow')
    })

    it('should still ask for bash in auto-edit mode', async () => {
      const result = await engine.check('bash', { command: 'make build' })
      expect(result.decision).toBe('ask_user')
    })
  })

  // =========================================================================
  // Critical Safety Rules
  // =========================================================================

  describe('safety rules', () => {
    it('should deny SSH key access', async () => {
      const result = await engine.check('write_file', {
        path: '/home/user/.ssh/id_rsa',
        content: 'evil',
      })
      expect(result.decision).toBe('deny')
      expect(result.denyMessage).toContain('SSH')
    })

    it('should deny /etc/passwd access', async () => {
      const result = await engine.check('write_file', {
        path: '/etc/passwd',
        content: 'evil',
      })
      expect(result.decision).toBe('deny')
    })

    it('should ask for .env file writes', async () => {
      const result = await engine.check('write_file', {
        path: '/project/.env',
        content: 'SECRET=x',
      })
      expect(result.decision).toBe('ask_user')
    })
  })

  // =========================================================================
  // Priority
  // =========================================================================

  describe('priority', () => {
    it('should use higher priority rules first', async () => {
      const engine = new PolicyEngine({
        rules: [
          {
            name: 'low-allow',
            toolName: 'bash',
            decision: 'allow',
            priority: 10,
            source: 'test',
          },
          {
            name: 'high-deny',
            toolName: 'bash',
            decision: 'deny',
            priority: 100,
            source: 'test',
          },
        ],
      })

      const result = await engine.check('bash', { command: 'ls' })
      expect(result.decision).toBe('deny')
      expect(result.matchedRule?.name).toBe('high-deny')
    })
  })

  // =========================================================================
  // Custom Rules
  // =========================================================================

  describe('custom rules', () => {
    it('should add and evaluate custom rules', async () => {
      engine.addRule({
        name: 'allow-npm-test',
        toolName: 'bash',
        argsPattern: /npm\s+test/,
        decision: 'allow',
        priority: 200,
        source: 'user',
      })

      const result = await engine.check('bash', { command: 'npm test' })
      expect(result.decision).toBe('allow')
      expect(result.matchedRule?.name).toBe('allow-npm-test')
    })

    it('should remove rules', () => {
      engine.addRule({
        name: 'test-rule',
        toolName: 'bash',
        decision: 'allow',
        priority: 200,
        source: 'test',
      })

      const removed = engine.removeRule('test-rule')
      expect(removed).toBe(true)

      const removedAgain = engine.removeRule('test-rule')
      expect(removedAgain).toBe(false)
    })
  })

  // =========================================================================
  // Wildcard Patterns
  // =========================================================================

  describe('wildcard patterns', () => {
    it('should match MCP tool wildcards', async () => {
      const engine = new PolicyEngine({
        rules: [
          {
            name: 'allow-mcp',
            toolName: 'mcp__*',
            decision: 'allow',
            priority: 100,
            source: 'test',
          },
        ],
      })

      const result = await engine.check('mcp__github__search', {})
      expect(result.decision).toBe('allow')
    })

    it('should match delegate wildcards', async () => {
      const engine = new PolicyEngine({
        rules: [
          {
            name: 'allow-delegate',
            toolName: 'delegate_*',
            decision: 'allow',
            priority: 100,
            source: 'test',
          },
        ],
      })

      const result = await engine.check('delegate_coder', {})
      expect(result.decision).toBe('allow')
    })

    it('should match universal wildcard', async () => {
      const engine = new PolicyEngine({
        rules: [
          {
            name: 'deny-all',
            toolName: '*',
            decision: 'deny',
            priority: 100,
            source: 'test',
          },
        ],
      })

      const result = await engine.check('any_tool', {})
      expect(result.decision).toBe('deny')
    })
  })

  // =========================================================================
  // Non-Interactive Mode
  // =========================================================================

  describe('non-interactive mode', () => {
    it('should convert ASK_USER to DENY', async () => {
      engine.setNonInteractive(true)

      const result = await engine.check('bash', { command: 'make build' })
      expect(result.decision).toBe('deny')
      expect(result.reason).toContain('Non-interactive')
    })

    it('should not affect ALLOW decisions', async () => {
      engine.setNonInteractive(true)

      const result = await engine.check('read_file', { path: '/foo.ts' })
      expect(result.decision).toBe('allow')
    })
  })

  // =========================================================================
  // Regex Args Matching
  // =========================================================================

  describe('regex args matching', () => {
    it('should match args against regex pattern', async () => {
      const engine = new PolicyEngine({
        rules: [
          {
            name: 'deny-rm-rf',
            toolName: 'bash',
            argsPattern: /rm\s+-rf/,
            decision: 'deny',
            priority: 500,
            source: 'test',
          },
          {
            name: 'allow-all-bash',
            toolName: 'bash',
            decision: 'allow',
            priority: 100,
            source: 'test',
          },
        ],
      })

      const denied = await engine.check('bash', { command: 'rm -rf /' })
      expect(denied.decision).toBe('deny')

      const allowed = await engine.check('bash', { command: 'ls -la' })
      expect(allowed.decision).toBe('allow')
    })
  })

  // =========================================================================
  // Safety Checkers
  // =========================================================================

  describe('safety checkers', () => {
    it('should run safety checkers after rule evaluation', async () => {
      const engine = new PolicyEngine({
        rules: [
          {
            name: 'allow-bash',
            toolName: 'bash',
            decision: 'allow',
            priority: 100,
            source: 'test',
          },
        ],
      })

      engine.addSafetyChecker({
        name: 'no-sudo',
        toolName: 'bash',
        priority: 100,
        async check(_toolName, args) {
          if (typeof args.command === 'string' && args.command.includes('sudo')) {
            return {
              decision: 'deny',
              reason: 'sudo commands are blocked',
              denyMessage: 'Cannot run sudo commands.',
            }
          }
          return null
        },
      })

      const allowed = await engine.check('bash', { command: 'ls' })
      expect(allowed.decision).toBe('allow')

      const denied = await engine.check('bash', { command: 'sudo rm -rf /' })
      expect(denied.decision).toBe('deny')
      expect(denied.denyMessage).toContain('sudo')
    })
  })
})
