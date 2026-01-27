/**
 * Delegation Tools Tests
 *
 * Tests for:
 * - BUG-12: Agent alias resolution
 * - BUG-14: Mission sync on delegate_task
 */

import { describe, it, expect } from 'vitest'
import { resolveAgentType } from '../../src/tools/delegation.js'

describe('resolveAgentType', () => {
  describe('alias resolution (BUG-12)', () => {
    it('resolves ui_ops to uiOps', () => {
      expect(resolveAgentType('ui_ops')).toBe('uiOps')
    })

    it('resolves explorer to scout', () => {
      expect(resolveAgentType('explorer')).toBe('scout')
    })

    it('resolves operator_complex to operator', () => {
      expect(resolveAgentType('operator_complex')).toBe('operator')
    })

    it('resolves validator_strict to validator', () => {
      expect(resolveAgentType('validator_strict')).toBe('validator')
    })
  })

  describe('registered names passthrough', () => {
    it('passes through operator unchanged', () => {
      expect(resolveAgentType('operator')).toBe('operator')
    })

    it('passes through validator unchanged', () => {
      expect(resolveAgentType('validator')).toBe('validator')
    })

    it('passes through scout unchanged', () => {
      expect(resolveAgentType('scout')).toBe('scout')
    })

    it('passes through uiOps unchanged', () => {
      expect(resolveAgentType('uiOps')).toBe('uiOps')
    })

    it('passes through qa unchanged', () => {
      expect(resolveAgentType('qa')).toBe('qa')
    })

    it('passes through council agents unchanged', () => {
      expect(resolveAgentType('cipher')).toBe('cipher')
      expect(resolveAgentType('vector')).toBe('vector')
      expect(resolveAgentType('prism')).toBe('prism')
      expect(resolveAgentType('apex')).toBe('apex')
    })
  })

  describe('unknown agents', () => {
    it('passes through unknown agent names', () => {
      expect(resolveAgentType('custom_agent')).toBe('custom_agent')
    })
  })
})
