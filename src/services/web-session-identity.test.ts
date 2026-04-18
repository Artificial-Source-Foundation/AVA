import { beforeEach, describe, expect, it } from 'vitest'

import { STORAGE_KEYS } from '../config/constants'

import {
  buildSessionBaseEndpoint,
  buildSessionEndpoint,
  canonicalizeSessionId,
  clearAllSessionIdMappings,
  getSessionMappingCount,
  hasBackendSessionMapping,
  registerBackendSessionId,
  rehydrateFromLocalStorageForTesting,
  resolveBackendSessionId,
  resolveFrontendSessionId,
  unregisterBackendSessionId,
} from './web-session-identity'

describe('web-session-identity', () => {
  beforeEach(() => {
    clearAllSessionIdMappings()
    // Clear localStorage for clean test state
    localStorage.removeItem(STORAGE_KEYS.SESSION_ID_ALIASES)
  })

  describe('registerBackendSessionId', () => {
    it('registers a mapping when frontend and backend IDs differ', () => {
      registerBackendSessionId('frontend-1', 'backend-1')

      expect(hasBackendSessionMapping('frontend-1')).toBe(true)
      expect(resolveBackendSessionId('frontend-1')).toBe('backend-1')
    })

    it('does not register a mapping when IDs are identical', () => {
      registerBackendSessionId('same-id', 'same-id')

      expect(hasBackendSessionMapping('same-id')).toBe(false)
      expect(getSessionMappingCount()).toBe(0)
    })

    it('overwrites existing mappings for the same frontend ID', () => {
      registerBackendSessionId('frontend-1', 'backend-1')
      registerBackendSessionId('frontend-1', 'backend-2')

      expect(resolveBackendSessionId('frontend-1')).toBe('backend-2')
      expect(getSessionMappingCount()).toBe(1)
    })
  })

  describe('resolveBackendSessionId', () => {
    it('returns the mapped backend ID when mapping exists', () => {
      registerBackendSessionId('frontend-1', 'backend-1')

      expect(resolveBackendSessionId('frontend-1')).toBe('backend-1')
    })

    it('returns the frontend ID as fallback when no mapping exists', () => {
      expect(resolveBackendSessionId('unknown-frontend')).toBe('unknown-frontend')
    })
  })

  describe('hasBackendSessionMapping', () => {
    it('returns true when a mapping exists', () => {
      registerBackendSessionId('frontend-1', 'backend-1')

      expect(hasBackendSessionMapping('frontend-1')).toBe(true)
    })

    it('returns false when no mapping exists', () => {
      expect(hasBackendSessionMapping('unknown-frontend')).toBe(false)
    })
  })

  describe('unregisterBackendSessionId', () => {
    it('removes an existing mapping', () => {
      registerBackendSessionId('frontend-1', 'backend-1')
      unregisterBackendSessionId('frontend-1')

      expect(hasBackendSessionMapping('frontend-1')).toBe(false)
      expect(resolveBackendSessionId('frontend-1')).toBe('frontend-1')
    })

    it('does nothing when mapping does not exist', () => {
      unregisterBackendSessionId('non-existent')

      expect(getSessionMappingCount()).toBe(0)
    })
  })

  describe('clearAllSessionIdMappings', () => {
    it('removes all mappings', () => {
      registerBackendSessionId('frontend-1', 'backend-1')
      registerBackendSessionId('frontend-2', 'backend-2')
      registerBackendSessionId('frontend-3', 'backend-3')

      expect(getSessionMappingCount()).toBe(3)

      clearAllSessionIdMappings()

      expect(getSessionMappingCount()).toBe(0)
      expect(hasBackendSessionMapping('frontend-1')).toBe(false)
    })
  })

  describe('getSessionMappingCount', () => {
    it('returns the number of registered mappings', () => {
      expect(getSessionMappingCount()).toBe(0)

      registerBackendSessionId('frontend-1', 'backend-1')
      expect(getSessionMappingCount()).toBe(1)

      registerBackendSessionId('frontend-2', 'backend-2')
      expect(getSessionMappingCount()).toBe(2)
    })
  })

  describe('replay parity (integration pattern)', () => {
    it('maintains correct resolution after multiple registrations', () => {
      // Simulate replay flow: original session, then retry creates new backend session
      const frontendId = 'session-frontend-abc'
      const originalBackendId = 'session-backend-original'
      const retryBackendId = 'session-backend-retry'

      // Initial registration
      registerBackendSessionId(frontendId, originalBackendId)
      expect(resolveBackendSessionId(frontendId)).toBe(originalBackendId)

      // Retry creates new backend session — mapping updates
      registerBackendSessionId(frontendId, retryBackendId)
      expect(resolveBackendSessionId(frontendId)).toBe(retryBackendId)

      // Resolution is stable after multiple calls
      expect(resolveBackendSessionId(frontendId)).toBe(retryBackendId)
      expect(resolveBackendSessionId(frontendId)).toBe(retryBackendId)
    })

    it('handles non-message endpoint mapping scenario', () => {
      // Simulate fetching session agents/files/terminal via backend ID
      const frontendId = 'fe-session-123'
      const backendId = 'be-session-456'

      // No mapping registered yet — should use frontend ID
      expect(resolveBackendSessionId(frontendId)).toBe(frontendId)

      // After run completes, mapping is registered
      registerBackendSessionId(frontendId, backendId)

      // Subsequent API calls resolve to backend ID
      const resolvedId = resolveBackendSessionId(frontendId)
      expect(resolvedId).toBe(backendId)

      // This would be used in URLs like /api/sessions/${resolvedId}/agents
      expect(resolvedId).not.toBe(frontendId)
    })
  })

  describe('buildSessionEndpoint', () => {
    it('builds endpoint URL with resolved backend session ID', () => {
      const frontendId = 'fe-session-123'
      const backendId = 'be-session-456'
      registerBackendSessionId(frontendId, backendId)

      const endpoint = buildSessionEndpoint(frontendId, 'agents')
      expect(endpoint).toContain(`/api/sessions/${backendId}/agents`)
      expect(endpoint).not.toContain(frontendId)
    })

    it('uses frontend ID as fallback when no mapping exists', () => {
      const frontendId = 'unmapped-session'

      const endpoint = buildSessionEndpoint(frontendId, 'files')
      expect(endpoint).toContain(`/api/sessions/${frontendId}/files`)
    })

    it('works for all subresource paths (non-message endpoint coverage)', () => {
      const frontendId = 'fe-test'
      const backendId = 'be-test'
      registerBackendSessionId(frontendId, backendId)

      // Test agents endpoint (representing non-message subresource)
      expect(buildSessionEndpoint(frontendId, 'agents')).toContain(
        `/api/sessions/${backendId}/agents`
      )

      // Test files endpoint
      expect(buildSessionEndpoint(frontendId, 'files')).toContain(
        `/api/sessions/${backendId}/files`
      )

      // Test terminal endpoint
      expect(buildSessionEndpoint(frontendId, 'terminal')).toContain(
        `/api/sessions/${backendId}/terminal`
      )

      // Test checkpoints endpoint
      expect(buildSessionEndpoint(frontendId, 'checkpoints')).toContain(
        `/api/sessions/${backendId}/checkpoints`
      )
    })
  })

  describe('buildSessionBaseEndpoint', () => {
    it('builds base endpoint with resolved backend session ID', () => {
      const frontendId = 'fe-session-789'
      const backendId = 'be-session-abc'
      registerBackendSessionId(frontendId, backendId)

      const endpoint = buildSessionBaseEndpoint(frontendId)
      expect(endpoint).toBe(`/api/sessions/${backendId}`)
    })

    it('builds endpoint with suffix and resolved ID', () => {
      const frontendId = 'fe-session-xyz'
      const backendId = 'be-session-uvw'
      registerBackendSessionId(frontendId, backendId)

      const endpoint = buildSessionBaseEndpoint(frontendId, 'rename')
      expect(endpoint).toBe(`/api/sessions/${backendId}/rename`)
    })

    it('uses frontend ID as fallback for base endpoint', () => {
      const frontendId = 'unmapped-base'

      const endpoint = buildSessionBaseEndpoint(frontendId, 'rename')
      expect(endpoint).toBe(`/api/sessions/${frontendId}/rename`)
    })
  })

  describe('lifecycle integration', () => {
    it('cleans up alias mapping on session removal (lifecycle path)', () => {
      const sessionId = 'session-to-delete'
      const backendId = 'backend-mapping'

      // Simulate successful run that created alias
      registerBackendSessionId(sessionId, backendId)
      expect(hasBackendSessionMapping(sessionId)).toBe(true)
      expect(resolveBackendSessionId(sessionId)).toBe(backendId)

      // Simulate session deletion lifecycle event
      unregisterBackendSessionId(sessionId)

      // Mapping should be cleaned up
      expect(hasBackendSessionMapping(sessionId)).toBe(false)
      expect(resolveBackendSessionId(sessionId)).toBe(sessionId)
    })

    it('cleans up alias on session archival (lifecycle path)', () => {
      const sessionId = 'session-to-archive'
      const backendId = 'backend-archive-mapping'

      registerBackendSessionId(sessionId, backendId)
      expect(hasBackendSessionMapping(sessionId)).toBe(true)

      // Simulate archive lifecycle event
      unregisterBackendSessionId(sessionId)

      expect(hasBackendSessionMapping(sessionId)).toBe(false)
    })

    it('handles duplicate unregistration gracefully', () => {
      const sessionId = 'already-gone'

      // Should not throw when unregistering non-existent mapping
      expect(() => unregisterBackendSessionId(sessionId)).not.toThrow()
      expect(hasBackendSessionMapping(sessionId)).toBe(false)
    })
  })

  describe('reverse alias resolution (backend → frontend)', () => {
    it('resolves frontend ID from backend ID', () => {
      const frontendId = 'fe-session-123'
      const backendId = 'be-session-456'

      registerBackendSessionId(frontendId, backendId)

      // Reverse resolution: backend ID → frontend ID
      expect(resolveFrontendSessionId(backendId)).toBe(frontendId)
    })

    it('returns backend ID unchanged if no reverse mapping exists', () => {
      const unmappedBackendId = 'be-unmapped-789'

      // No mapping exists, should return the input unchanged
      expect(resolveFrontendSessionId(unmappedBackendId)).toBe(unmappedBackendId)
    })

    it('handles multiple sessions with bidirectional resolution', () => {
      const fe1 = 'fe-session-1'
      const be1 = 'be-session-1'
      const fe2 = 'fe-session-2'
      const be2 = 'be-session-2'

      registerBackendSessionId(fe1, be1)
      registerBackendSessionId(fe2, be2)

      // Forward resolution: frontend → backend
      expect(resolveBackendSessionId(fe1)).toBe(be1)
      expect(resolveBackendSessionId(fe2)).toBe(be2)

      // Reverse resolution: backend → frontend
      expect(resolveFrontendSessionId(be1)).toBe(fe1)
      expect(resolveFrontendSessionId(be2)).toBe(fe2)
    })

    it('canonicalizes session IDs correctly', () => {
      const frontendId = 'fe-canonical'
      const backendId = 'be-backend-divergent'

      registerBackendSessionId(frontendId, backendId)

      // canonicalizeSessionId is an alias for resolveFrontendSessionId
      expect(canonicalizeSessionId(backendId)).toBe(frontendId)
      expect(canonicalizeSessionId(frontendId)).toBe(frontendId) // Already canonical
    })

    it('handles latest mapping on re-registration', () => {
      const frontendId = 'fe-session-latest'
      const backendId1 = 'be-backend-original'
      const backendId2 = 'be-backend-retry'

      // First registration
      registerBackendSessionId(frontendId, backendId1)
      expect(resolveFrontendSessionId(backendId1)).toBe(frontendId)

      // Re-registration (e.g., after retry creates new backend session)
      registerBackendSessionId(frontendId, backendId2)

      // Old backend ID should no longer resolve
      expect(resolveFrontendSessionId(backendId1)).toBe(backendId1)
      // New backend ID should resolve
      expect(resolveFrontendSessionId(backendId2)).toBe(frontendId)
    })
  })

  describe('lifecycle integration', () => {
    it('cleans up alias mapping on session removal (lifecycle path)', () => {
      const sessionId = 'session-to-delete'
      const backendId = 'backend-mapping'

      // Simulate successful run that created alias
      registerBackendSessionId(sessionId, backendId)
      expect(hasBackendSessionMapping(sessionId)).toBe(true)
      expect(resolveBackendSessionId(sessionId)).toBe(backendId)

      // Simulate session deletion lifecycle event
      unregisterBackendSessionId(sessionId)

      // Mapping should be cleaned up
      expect(hasBackendSessionMapping(sessionId)).toBe(false)
      expect(resolveBackendSessionId(sessionId)).toBe(sessionId)
    })

    it('preserves alias on session archival so later operations still resolve', () => {
      const sessionId = 'session-to-archive'
      const backendId = 'backend-archive-mapping'

      registerBackendSessionId(sessionId, backendId)
      expect(hasBackendSessionMapping(sessionId)).toBe(true)

      // Simulate archive lifecycle event
      // Archiving must not unregister the alias because the backend session still
      // exists for archived list/load/unarchive flows.

      expect(hasBackendSessionMapping(sessionId)).toBe(true)
      expect(resolveBackendSessionId(sessionId)).toBe(backendId)
    })

    it('handles duplicate unregistration gracefully', () => {
      const sessionId = 'already-gone'

      // Should not throw when unregistering non-existent mapping
      expect(() => unregisterBackendSessionId(sessionId)).not.toThrow()
      expect(hasBackendSessionMapping(sessionId)).toBe(false)
    })
  })

  describe('reload persistence', () => {
    it('persists alias to localStorage on registration', () => {
      const frontendId = 'fe-session-persist'
      const backendId = 'be-session-persist'

      registerBackendSessionId(frontendId, backendId)

      const stored = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
      expect(stored).toBeTruthy()
      const parsed = JSON.parse(stored!)
      expect(parsed[frontendId]).toBe(backendId)
    })

    it('removes alias from localStorage on unregistration', () => {
      const frontendId = 'fe-session-remove'
      const backendId = 'be-session-remove'

      registerBackendSessionId(frontendId, backendId)
      expect(localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)).toContain(frontendId)

      unregisterBackendSessionId(frontendId)

      const stored = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
      const parsed = JSON.parse(stored || '{}')
      expect(parsed[frontendId]).toBeUndefined()
    })

    it('alias survives a fresh module reload (simulated)', () => {
      const frontendId = 'fe-survivor'
      const backendId = 'be-survivor'

      // Register an alias - this persists to localStorage
      registerBackendSessionId(frontendId, backendId)
      expect(resolveBackendSessionId(frontendId)).toBe(backendId)

      // Verify it's in localStorage
      const stored = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
      expect(stored).toBeTruthy()

      // Clear the in-memory map (simulates module reload) without clearing localStorage
      // This simulates a browser reload where the module is fresh but localStorage
      // still contains the persisted aliases

      // Clear everything including localStorage
      clearAllSessionIdMappings()
      // Restore localStorage state
      localStorage.setItem(STORAGE_KEYS.SESSION_ID_ALIASES, stored!)

      // Re-hydrate from localStorage (simulates the module's hydrate on load)
      rehydrateFromLocalStorageForTesting()

      // After reload simulation, the alias should still resolve
      expect(resolveBackendSessionId(frontendId)).toBe(backendId)
    })

    it('does not write to localStorage when identical IDs are registered', () => {
      const frontendId = 'fe-no-difference'

      registerBackendSessionId(frontendId, frontendId)

      const stored = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
      const parsed = JSON.parse(stored || '{}')
      expect(parsed[frontendId]).toBeUndefined()
    })

    it('does not write to localStorage when unregistering non-existent mapping', () => {
      const frontendId = 'fe-never-registered'

      // Pre-populate with one entry
      registerBackendSessionId('other-fe', 'other-be')
      const before = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)

      // Unregister something that doesn't exist
      unregisterBackendSessionId(frontendId)

      const after = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
      expect(after).toBe(before) // No change
    })

    it('survives canonicalization after simulated reload', () => {
      const frontendId = 'fe-canonical-reload'
      const backendId = 'be-canonical-reload'

      // Register and persist
      registerBackendSessionId(frontendId, backendId)
      const stored = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
      expect(stored).toBeTruthy()

      // Simulate reload: clear memory, restore localStorage, rehydrate
      clearAllSessionIdMappings()
      localStorage.setItem(STORAGE_KEYS.SESSION_ID_ALIASES, stored!)
      rehydrateFromLocalStorageForTesting()

      // Verify canonicalization still works after "reload"
      expect(canonicalizeSessionId(backendId)).toBe(frontendId)
      expect(canonicalizeSessionId(frontendId)).toBe(frontendId)
    })

    it('removes persisted alias from localStorage on delete cleanup', () => {
      const frontendId = 'fe-delete-cleanup'
      const backendId = 'be-delete-cleanup'

      // Register an alias - it should be in localStorage
      registerBackendSessionId(frontendId, backendId)
      let stored = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
      expect(stored).toContain(frontendId)
      expect(stored).toContain(backendId)

      // Simulate delete cleanup (unregister)
      unregisterBackendSessionId(frontendId)

      // The alias should be removed from localStorage
      stored = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
      const parsed = JSON.parse(stored || '{}')
      expect(parsed[frontendId]).toBeUndefined()
      expect(hasBackendSessionMapping(frontendId)).toBe(false)
    })

    it('persists multiple aliases and cleans them individually', () => {
      const sessions = [
        { fe: 'fe-1', be: 'be-1' },
        { fe: 'fe-2', be: 'be-2' },
        { fe: 'fe-3', be: 'be-3' },
      ]

      // Register all aliases
      sessions.forEach(({ fe, be }) => {
        registerBackendSessionId(fe, be)
      })

      let stored = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
      expect(stored).toBeTruthy()
      const parsedAll = JSON.parse(stored!)
      expect(Object.keys(parsedAll)).toHaveLength(3)

      // Delete only one
      unregisterBackendSessionId('fe-2')

      // Verify others remain in localStorage
      stored = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
      const parsedRemaining = JSON.parse(stored!)
      expect(parsedRemaining['fe-1']).toBe('be-1')
      expect(parsedRemaining['fe-2']).toBeUndefined()
      expect(parsedRemaining['fe-3']).toBe('be-3')
    })

    it('clears all persisted aliases with clearAllSessionIdMappings', () => {
      const sessions = [
        { fe: 'fe-a', be: 'be-a' },
        { fe: 'fe-b', be: 'be-b' },
      ]

      // Register all aliases
      sessions.forEach(({ fe, be }) => {
        registerBackendSessionId(fe, be)
      })

      // Verify they're persisted
      expect(localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)).toBeTruthy()

      // Clear all
      clearAllSessionIdMappings()

      // localStorage should be empty
      expect(localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)).toBeNull()
      expect(getSessionMappingCount()).toBe(0)
    })

    it('handles reload with corrupted localStorage gracefully', () => {
      // Put corrupted data in localStorage
      localStorage.setItem(STORAGE_KEYS.SESSION_ID_ALIASES, 'not-valid-json')

      // Rehydrate should not throw
      expect(() => rehydrateFromLocalStorageForTesting()).not.toThrow()

      // Map should be empty
      expect(getSessionMappingCount()).toBe(0)
    })

    it('handles reload with non-object JSON gracefully', () => {
      // Put an array instead of an object in localStorage
      localStorage.setItem(STORAGE_KEYS.SESSION_ID_ALIASES, JSON.stringify(['fe-1', 'be-1']))

      // Rehydrate should not throw
      expect(() => rehydrateFromLocalStorageForTesting()).not.toThrow()

      // Map should be empty (arrays are rejected)
      expect(getSessionMappingCount()).toBe(0)
    })
  })
})
