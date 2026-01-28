/**
 * Delegation Tools Tests
 *
 * Tests for:
 * - BUG-12: Agent alias resolution
 * - BUG-14: Mission sync on delegate_task
 * - 3-tier Marine system aliases
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

    it('resolves operator_complex to operator_tier3', () => {
      // Changed: Now maps to tier3 (Delta Force) instead of operator
      expect(resolveAgentType('operator_complex')).toBe('operator_tier3')
    })

    it('resolves validator_strict to validator', () => {
      expect(resolveAgentType('validator_strict')).toBe('validator')
    })
  })

  describe('3-tier Marine system aliases', () => {
    it('resolves operator to operator_tier2', () => {
      expect(resolveAgentType('operator')).toBe('operator_tier2')
    })

    it('resolves marine aliases to tiers', () => {
      expect(resolveAgentType('marine_private')).toBe('operator_tier1')
      expect(resolveAgentType('marine_sergeant')).toBe('operator_tier2')
      expect(resolveAgentType('delta_force')).toBe('operator_tier3')
      expect(resolveAgentType('marine')).toBe('operator_tier2')
    })

    it('resolves hyphenated marine aliases', () => {
      expect(resolveAgentType('marine-private')).toBe('operator_tier1')
      expect(resolveAgentType('marine-sergeant')).toBe('operator_tier2')
      expect(resolveAgentType('delta-force')).toBe('operator_tier3')
    })
  })

  describe('registered names passthrough', () => {
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
      expect(resolveAgentType('apex')).toBe('apex')
      // New strategic advisors
      expect(resolveAgentType('aegis')).toBe('aegis')
      expect(resolveAgentType('razor')).toBe('razor')
      expect(resolveAgentType('oracle')).toBe('oracle')
    })

    it('passes through tier names unchanged', () => {
      expect(resolveAgentType('operator_tier1')).toBe('operator_tier1')
      expect(resolveAgentType('operator_tier2')).toBe('operator_tier2')
      expect(resolveAgentType('operator_tier3')).toBe('operator_tier3')
    })
  })

  describe('unknown agents', () => {
    it('passes through unknown agent names', () => {
      expect(resolveAgentType('custom_agent')).toBe('custom_agent')
    })
  })
})
