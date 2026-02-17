/**
 * Minimal Mode Tests
 * Verifies minimal mode restricts tool access correctly
 */

import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import {
  checkMinimalModeAccess,
  clearAllMinimalModeStates,
  enterMinimalMode,
  exitMinimalMode,
  isMinimalModeActive,
  MINIMAL_MODE_ALLOWED_TOOLS,
} from './minimal.js'

// ============================================================================
// Setup
// ============================================================================

beforeEach(() => {
  clearAllMinimalModeStates()
})

afterEach(() => {
  clearAllMinimalModeStates()
})

// ============================================================================
// Tests
// ============================================================================

describe('Minimal Mode', () => {
  describe('state management', () => {
    it('is inactive by default', () => {
      expect(isMinimalModeActive()).toBe(false)
      expect(isMinimalModeActive('session-1')).toBe(false)
    })

    it('enters and exits minimal mode', () => {
      enterMinimalMode()
      expect(isMinimalModeActive()).toBe(true)

      exitMinimalMode()
      expect(isMinimalModeActive()).toBe(false)
    })

    it('isolates per-session state', () => {
      enterMinimalMode('session-1')
      expect(isMinimalModeActive('session-1')).toBe(true)
      expect(isMinimalModeActive('session-2')).toBe(false)

      enterMinimalMode('session-2')
      expect(isMinimalModeActive('session-1')).toBe(true)
      expect(isMinimalModeActive('session-2')).toBe(true)

      exitMinimalMode('session-1')
      expect(isMinimalModeActive('session-1')).toBe(false)
      expect(isMinimalModeActive('session-2')).toBe(true)
    })

    it('clearAllMinimalModeStates resets everything', () => {
      enterMinimalMode('s1')
      enterMinimalMode('s2')
      clearAllMinimalModeStates()
      expect(isMinimalModeActive('s1')).toBe(false)
      expect(isMinimalModeActive('s2')).toBe(false)
    })
  })

  describe('tool access', () => {
    it('allows all tools when minimal mode inactive', () => {
      expect(checkMinimalModeAccess('bash').allowed).toBe(true)
      expect(checkMinimalModeAccess('browser').allowed).toBe(true)
      expect(checkMinimalModeAccess('websearch').allowed).toBe(true)
    })

    it('allows core tools in minimal mode', () => {
      enterMinimalMode()

      for (const tool of MINIMAL_MODE_ALLOWED_TOOLS) {
        const result = checkMinimalModeAccess(tool)
        expect(result.allowed).toBe(true)
      }
    })

    it('blocks non-allowed tools in minimal mode', () => {
      enterMinimalMode()

      const blockedTools = [
        'browser',
        'websearch',
        'webfetch',
        'task',
        'batch',
        'codesearch',
        'ls',
        'delete_file',
        'create_file',
        'apply_patch',
        'multiedit',
        'skill',
      ]

      for (const tool of blockedTools) {
        const result = checkMinimalModeAccess(tool)
        expect(result.allowed).toBe(false)
        expect(result.error).toBeDefined()
        expect(result.error!.error).toBe('MINIMAL_MODE_RESTRICTED')
      }
    })

    it('returns descriptive error message', () => {
      enterMinimalMode()
      const result = checkMinimalModeAccess('browser')
      expect(result.allowed).toBe(false)
      expect(result.error!.output).toContain('browser')
      expect(result.error!.output).toContain('minimal mode')
    })

    it('uses session-specific state for access checks', () => {
      enterMinimalMode('s1')

      // Session 1 is in minimal mode
      expect(checkMinimalModeAccess('browser', 's1').allowed).toBe(false)

      // Session 2 is not
      expect(checkMinimalModeAccess('browser', 's2').allowed).toBe(true)
    })
  })

  describe('allowed tools list', () => {
    it('includes essential file operations', () => {
      expect(MINIMAL_MODE_ALLOWED_TOOLS).toContain('read_file')
      expect(MINIMAL_MODE_ALLOWED_TOOLS).toContain('write_file')
      expect(MINIMAL_MODE_ALLOWED_TOOLS).toContain('edit')
    })

    it('includes search tools', () => {
      expect(MINIMAL_MODE_ALLOWED_TOOLS).toContain('glob')
      expect(MINIMAL_MODE_ALLOWED_TOOLS).toContain('grep')
    })

    it('includes bash for execution', () => {
      expect(MINIMAL_MODE_ALLOWED_TOOLS).toContain('bash')
    })

    it('includes completion tools', () => {
      expect(MINIMAL_MODE_ALLOWED_TOOLS).toContain('attempt_completion')
      expect(MINIMAL_MODE_ALLOWED_TOOLS).toContain('complete_task')
      expect(MINIMAL_MODE_ALLOWED_TOOLS).toContain('question')
    })
  })
})
