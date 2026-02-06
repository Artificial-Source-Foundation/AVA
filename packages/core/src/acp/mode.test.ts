/**
 * ACP Mode Manager Tests
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'
import { AcpModeManager, createAcpModeManager } from './mode.js'
import type { AcpTransport } from './types.js'
import { AcpErrorCode } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

function mockTransport(): AcpTransport {
  return {
    request: vi.fn(async () => undefined) as AcpTransport['request'],
    notify: vi.fn(),
  }
}

// ============================================================================
// Tests
// ============================================================================

describe('AcpModeManager', () => {
  let modeManager: AcpModeManager

  beforeEach(() => {
    modeManager = new AcpModeManager()
  })

  describe('getMode', () => {
    it('should default to agent mode', () => {
      expect(modeManager.getMode('session-1')).toBe('agent')
    })
  })

  describe('setMode', () => {
    it('should set mode to plan', () => {
      modeManager.setMode('session-1', 'plan')
      expect(modeManager.getMode('session-1')).toBe('plan')
    })

    it('should set mode to agent', () => {
      modeManager.setMode('session-1', 'plan')
      modeManager.setMode('session-1', 'agent')
      expect(modeManager.getMode('session-1')).toBe('agent')
    })

    it('should throw for invalid mode', () => {
      try {
        modeManager.setMode('session-1', 'invalid' as 'agent')
        expect.unreachable('Should have thrown')
      } catch (error) {
        expect((error as { code: number }).code).toBe(AcpErrorCode.INVALID_MODE)
      }
    })
  })

  describe('isToolAllowed', () => {
    it('should allow all tools in agent mode', () => {
      expect(modeManager.isToolAllowed('s1', 'bash')).toBe(true)
      expect(modeManager.isToolAllowed('s1', 'write')).toBe(true)
      expect(modeManager.isToolAllowed('s1', 'edit')).toBe(true)
      expect(modeManager.isToolAllowed('s1', 'delete')).toBe(true)
    })

    it('should restrict tools in plan mode', () => {
      modeManager.setMode('s1', 'plan')

      // Allowed
      expect(modeManager.isToolAllowed('s1', 'glob')).toBe(true)
      expect(modeManager.isToolAllowed('s1', 'grep')).toBe(true)
      expect(modeManager.isToolAllowed('s1', 'read')).toBe(true)
      expect(modeManager.isToolAllowed('s1', 'ls')).toBe(true)
      expect(modeManager.isToolAllowed('s1', 'websearch')).toBe(true)
      expect(modeManager.isToolAllowed('s1', 'webfetch')).toBe(true)
      expect(modeManager.isToolAllowed('s1', 'todoread')).toBe(true)

      // Blocked
      expect(modeManager.isToolAllowed('s1', 'bash')).toBe(false)
      expect(modeManager.isToolAllowed('s1', 'write')).toBe(false)
      expect(modeManager.isToolAllowed('s1', 'edit')).toBe(false)
      expect(modeManager.isToolAllowed('s1', 'create')).toBe(false)
      expect(modeManager.isToolAllowed('s1', 'delete')).toBe(false)
      expect(modeManager.isToolAllowed('s1', 'browser')).toBe(false)
    })
  })

  describe('getAllowedTools', () => {
    it('should return null in agent mode (all tools)', () => {
      expect(modeManager.getAllowedTools('s1')).toBeNull()
    })

    it('should return tool list in plan mode', () => {
      modeManager.setMode('s1', 'plan')

      const tools = modeManager.getAllowedTools('s1')
      expect(tools).not.toBeNull()
      expect(tools).toContain('glob')
      expect(tools).toContain('grep')
      expect(tools).toContain('read')
      expect(tools).not.toContain('bash')
      expect(tools).not.toContain('write')
    })
  })

  describe('isPlanMode', () => {
    it('should return false by default', () => {
      expect(modeManager.isPlanMode('s1')).toBe(false)
    })

    it('should return true in plan mode', () => {
      modeManager.setMode('s1', 'plan')
      expect(modeManager.isPlanMode('s1')).toBe(true)
    })
  })

  describe('initSession', () => {
    it('should initialize with default mode', () => {
      modeManager.initSession('new-1')
      expect(modeManager.getMode('new-1')).toBe('agent')
    })

    it('should initialize with specified mode', () => {
      modeManager.initSession('new-2', 'plan')
      expect(modeManager.getMode('new-2')).toBe('plan')
    })
  })

  describe('removeSession', () => {
    it('should remove mode tracking', () => {
      modeManager.setMode('rem-1', 'plan')
      modeManager.removeSession('rem-1')
      expect(modeManager.getMode('rem-1')).toBe('agent') // Back to default
    })
  })

  describe('onModeChange', () => {
    it('should emit events on mode change', () => {
      const listener = vi.fn()
      modeManager.onModeChange(listener)

      modeManager.setMode('s1', 'plan')

      expect(listener).toHaveBeenCalledWith({
        sessionId: 's1',
        mode: 'plan',
        previousMode: 'agent',
      })
    })

    it('should not emit when mode stays the same', () => {
      const listener = vi.fn()
      modeManager.onModeChange(listener)

      modeManager.initSession('s1', 'agent')
      modeManager.setMode('s1', 'agent')

      expect(listener).not.toHaveBeenCalled()
    })

    it('should support unsubscribe', () => {
      const listener = vi.fn()
      const unsubscribe = modeManager.onModeChange(listener)

      modeManager.setMode('s1', 'plan')
      expect(listener).toHaveBeenCalledTimes(1)

      unsubscribe()
      modeManager.setMode('s1', 'agent')
      expect(listener).toHaveBeenCalledTimes(1) // Not called again
    })

    it('should notify transport of mode change', () => {
      const transport = mockTransport()
      modeManager.setTransport(transport)

      modeManager.setMode('s1', 'plan')

      expect(transport.notify).toHaveBeenCalledWith('session/mode_changed', {
        sessionId: 's1',
        mode: 'plan',
      })
    })
  })

  describe('factory', () => {
    it('should create manager with factory', () => {
      const m = createAcpModeManager()
      expect(m).toBeInstanceOf(AcpModeManager)
    })
  })
})
