/**
 * Tests for Delta9 Injection Tracker
 */

import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import {
  InjectionTracker,
  getInjectionTracker,
  clearInjectionTracker,
  hasInjected,
  tryInject,
  clearSessionInjections,
  CONTEXT_TYPES,
} from '../../src/lib/injection-tracker.js'

describe('InjectionTracker', () => {
  let tracker: InjectionTracker

  beforeEach(() => {
    tracker = new InjectionTracker()
  })

  afterEach(() => {
    tracker.clear()
  })

  describe('hasInjected', () => {
    it('returns false for untracked session', () => {
      expect(tracker.hasInjected('session-1', 'mission_briefing')).toBe(false)
    })

    it('returns false for untracked context type', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      expect(tracker.hasInjected('session-1', 'agent_role')).toBe(false)
    })

    it('returns true for tracked context type', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      expect(tracker.hasInjected('session-1', 'mission_briefing')).toBe(true)
    })
  })

  describe('markInjected', () => {
    it('marks context type as injected', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      expect(tracker.hasInjected('session-1', 'mission_briefing')).toBe(true)
    })

    it('tracks multiple context types per session', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      tracker.markInjected('session-1', 'agent_role')
      tracker.markInjected('session-1', 'tool_hints')

      expect(tracker.hasInjected('session-1', 'mission_briefing')).toBe(true)
      expect(tracker.hasInjected('session-1', 'agent_role')).toBe(true)
      expect(tracker.hasInjected('session-1', 'tool_hints')).toBe(true)
    })

    it('isolates sessions', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      tracker.markInjected('session-2', 'agent_role')

      expect(tracker.hasInjected('session-1', 'mission_briefing')).toBe(true)
      expect(tracker.hasInjected('session-1', 'agent_role')).toBe(false)
      expect(tracker.hasInjected('session-2', 'mission_briefing')).toBe(false)
      expect(tracker.hasInjected('session-2', 'agent_role')).toBe(true)
    })

    it('records size', () => {
      tracker.markInjected('session-1', 'mission_briefing', 1500)
      const records = tracker.getInjectionRecords('session-1')

      expect(records).toHaveLength(1)
      expect(records[0].size).toBe(1500)
    })
  })

  describe('tryInject', () => {
    it('returns true and marks first injection', () => {
      const result = tracker.tryInject('session-1', 'mission_briefing')

      expect(result).toBe(true)
      expect(tracker.hasInjected('session-1', 'mission_briefing')).toBe(true)
    })

    it('returns false for duplicate injection', () => {
      tracker.tryInject('session-1', 'mission_briefing')
      const result = tracker.tryInject('session-1', 'mission_briefing')

      expect(result).toBe(false)
    })

    it('tracks duplicates prevented', () => {
      tracker.tryInject('session-1', 'mission_briefing')
      tracker.tryInject('session-1', 'mission_briefing')
      tracker.tryInject('session-1', 'mission_briefing')

      const stats = tracker.getStats()
      expect(stats.duplicatesPrevented).toBe(2)
    })

    it('allows different context types', () => {
      const r1 = tracker.tryInject('session-1', 'mission_briefing')
      const r2 = tracker.tryInject('session-1', 'agent_role')

      expect(r1).toBe(true)
      expect(r2).toBe(true)
    })
  })

  describe('clearSession', () => {
    it('clears injection tracking for a session', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      tracker.markInjected('session-1', 'agent_role')
      expect(tracker.hasInjected('session-1', 'mission_briefing')).toBe(true)

      tracker.clearSession('session-1')

      expect(tracker.hasInjected('session-1', 'mission_briefing')).toBe(false)
      expect(tracker.hasInjected('session-1', 'agent_role')).toBe(false)
    })

    it('does not affect other sessions', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      tracker.markInjected('session-2', 'mission_briefing')

      tracker.clearSession('session-1')

      expect(tracker.hasInjected('session-1', 'mission_briefing')).toBe(false)
      expect(tracker.hasInjected('session-2', 'mission_briefing')).toBe(true)
    })

    it('is safe to call on non-existent session', () => {
      expect(() => tracker.clearSession('non-existent')).not.toThrow()
    })
  })

  describe('getInjectedTypes', () => {
    it('returns all injected context types', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      tracker.markInjected('session-1', 'agent_role')
      tracker.markInjected('session-1', 'tool_hints')

      const types = tracker.getInjectedTypes('session-1')

      expect(types).toHaveLength(3)
      expect(types).toContain('mission_briefing')
      expect(types).toContain('agent_role')
      expect(types).toContain('tool_hints')
    })

    it('returns empty array for unknown session', () => {
      const types = tracker.getInjectedTypes('unknown')
      expect(types).toEqual([])
    })
  })

  describe('getInjectionRecords', () => {
    it('returns injection records with timestamps', () => {
      tracker.markInjected('session-1', 'mission_briefing', 1000)
      tracker.markInjected('session-1', 'agent_role', 500)

      const records = tracker.getInjectionRecords('session-1')

      expect(records).toHaveLength(2)
      expect(records[0].contextType).toBe('mission_briefing')
      expect(records[0].size).toBe(1000)
      expect(records[0].timestamp).toBeDefined()
      expect(records[1].contextType).toBe('agent_role')
      expect(records[1].size).toBe(500)
    })

    it('returns empty array for unknown session', () => {
      const records = tracker.getInjectionRecords('unknown')
      expect(records).toEqual([])
    })
  })

  describe('hasSession', () => {
    it('returns true for session with injections', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      expect(tracker.hasSession('session-1')).toBe(true)
    })

    it('returns false for unknown session', () => {
      expect(tracker.hasSession('unknown')).toBe(false)
    })
  })

  describe('getStats', () => {
    it('tracks total injections', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      tracker.markInjected('session-1', 'agent_role')
      tracker.markInjected('session-2', 'mission_briefing')

      const stats = tracker.getStats()
      expect(stats.totalInjections).toBe(3)
    })

    it('tracks active sessions', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      tracker.markInjected('session-2', 'mission_briefing')

      expect(tracker.getStats().activeSessions).toBe(2)

      tracker.clearSession('session-1')

      expect(tracker.getStats().activeSessions).toBe(1)
    })

    it('tracks duplicates prevented', () => {
      tracker.tryInject('session-1', 'mission_briefing')
      tracker.tryInject('session-1', 'mission_briefing')

      expect(tracker.getStats().duplicatesPrevented).toBe(1)
    })
  })

  describe('getActiveSessions', () => {
    it('returns all session IDs with injections', () => {
      tracker.markInjected('session-1', 'test')
      tracker.markInjected('session-2', 'test')
      tracker.markInjected('session-3', 'test')

      const sessions = tracker.getActiveSessions()

      expect(sessions).toHaveLength(3)
      expect(sessions).toContain('session-1')
      expect(sessions).toContain('session-2')
      expect(sessions).toContain('session-3')
    })
  })

  describe('getSessionInjectionSize', () => {
    it('returns total size of all injections', () => {
      tracker.markInjected('session-1', 'mission_briefing', 1000)
      tracker.markInjected('session-1', 'agent_role', 500)
      tracker.markInjected('session-1', 'tool_hints', 250)

      const size = tracker.getSessionInjectionSize('session-1')
      expect(size).toBe(1750)
    })

    it('returns 0 for unknown session', () => {
      expect(tracker.getSessionInjectionSize('unknown')).toBe(0)
    })

    it('handles injections without size', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      tracker.markInjected('session-1', 'agent_role', 500)

      const size = tracker.getSessionInjectionSize('session-1')
      expect(size).toBe(500) // Only the one with size
    })
  })

  describe('clear', () => {
    it('clears all tracking data', () => {
      tracker.markInjected('session-1', 'mission_briefing')
      tracker.markInjected('session-2', 'agent_role')
      tracker.tryInject('session-1', 'mission_briefing') // duplicate

      tracker.clear()

      expect(tracker.hasInjected('session-1', 'mission_briefing')).toBe(false)
      expect(tracker.hasInjected('session-2', 'agent_role')).toBe(false)
      expect(tracker.getStats().totalInjections).toBe(0)
      expect(tracker.getStats().duplicatesPrevented).toBe(0)
      expect(tracker.getStats().activeSessions).toBe(0)
    })
  })
})

describe('singleton functions', () => {
  beforeEach(() => {
    clearInjectionTracker()
  })

  afterEach(() => {
    clearInjectionTracker()
  })

  it('getInjectionTracker returns singleton', () => {
    const tracker1 = getInjectionTracker()
    const tracker2 = getInjectionTracker()
    expect(tracker1).toBe(tracker2)
  })

  it('hasInjected checks singleton', () => {
    getInjectionTracker().markInjected('session-1', 'test')
    expect(hasInjected('session-1', 'test')).toBe(true)
  })

  it('tryInject uses singleton', () => {
    expect(tryInject('session-1', 'test')).toBe(true)
    expect(tryInject('session-1', 'test')).toBe(false)
  })

  it('clearSessionInjections clears from singleton', () => {
    tryInject('session-1', 'test')
    expect(hasInjected('session-1', 'test')).toBe(true)

    clearSessionInjections('session-1')
    expect(hasInjected('session-1', 'test')).toBe(false)
  })
})

describe('CONTEXT_TYPES', () => {
  it('defines standard context types', () => {
    expect(CONTEXT_TYPES.MISSION_BRIEFING).toBe('mission_briefing')
    expect(CONTEXT_TYPES.AGENT_ROLE).toBe('agent_role')
    expect(CONTEXT_TYPES.TOOL_HINTS).toBe('tool_hints')
    expect(CONTEXT_TYPES.PROJECT_CONFIG).toBe('project_config')
    expect(CONTEXT_TYPES.WORKFLOW_INSTRUCTIONS).toBe('workflow_instructions')
    expect(CONTEXT_TYPES.MEMORY_CONTEXT).toBe('memory_context')
    expect(CONTEXT_TYPES.GUARDIAN_WARNING).toBe('guardian_warning')
  })

  it('can be used with tracker', () => {
    const tracker = new InjectionTracker()

    tracker.markInjected('session-1', CONTEXT_TYPES.MISSION_BRIEFING)
    tracker.markInjected('session-1', CONTEXT_TYPES.AGENT_ROLE)

    expect(tracker.hasInjected('session-1', CONTEXT_TYPES.MISSION_BRIEFING)).toBe(true)
    expect(tracker.hasInjected('session-1', CONTEXT_TYPES.AGENT_ROLE)).toBe(true)
    expect(tracker.hasInjected('session-1', CONTEXT_TYPES.TOOL_HINTS)).toBe(false)
  })
})
