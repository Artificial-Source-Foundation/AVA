import { beforeEach, describe, expect, it, vi } from 'vitest'

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

/** localStorage key prefix for per-alias storage (must match implementation). */
const ALIAS_KEY_PREFIX = 'ava_sa:'
const LEGACY_ALIAS_KEY = 'ava_session_id_aliases'

/** Build storage key for a specific alias. */
function aliasKey(frontendId: string): string {
  return `${ALIAS_KEY_PREFIX}${frontendId}`
}

/** Read all aliases from storage as a Record for test assertions. */
function readAllStoredAliases(): Record<string, string> {
  const result: Record<string, string> = {}
  for (let i = 0; i < localStorage.length; i++) {
    const key = localStorage.key(i)
    if (key?.startsWith(ALIAS_KEY_PREFIX)) {
      const frontendId = key.slice(ALIAS_KEY_PREFIX.length)
      const backendId = localStorage.getItem(key)
      if (backendId) {
        result[frontendId] = backendId
      }
    }
  }
  return result
}

describe('web-session-identity', () => {
  beforeEach(() => {
    clearAllSessionIdMappings()
    localStorage.removeItem(LEGACY_ALIAS_KEY)
    // Clear any remaining per-alias keys (safety net)
    const keysToRemove: string[] = []
    for (let i = 0; i < localStorage.length; i++) {
      const key = localStorage.key(i)
      if (key?.startsWith(ALIAS_KEY_PREFIX)) {
        keysToRemove.push(key)
      }
    }
    keysToRemove.forEach((key) => {
      localStorage.removeItem(key)
    })
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

  describe('reload persistence', () => {
    it('persists alias to localStorage on registration', () => {
      const frontendId = 'fe-session-persist'
      const backendId = 'be-session-persist'

      registerBackendSessionId(frontendId, backendId)

      // With per-alias keys, check the specific key
      expect(localStorage.getItem(aliasKey(frontendId))).toBe(backendId)
    })

    it('removes alias from localStorage on unregistration', () => {
      const frontendId = 'fe-session-remove'
      const backendId = 'be-session-remove'

      registerBackendSessionId(frontendId, backendId)
      expect(localStorage.getItem(aliasKey(frontendId))).toBe(backendId)

      unregisterBackendSessionId(frontendId)

      expect(localStorage.getItem(aliasKey(frontendId))).toBeNull()
    })

    it('alias survives a fresh module reload (simulated)', () => {
      const frontendId = 'fe-survivor'
      const backendId = 'be-survivor'

      // Register an alias - this persists to localStorage
      registerBackendSessionId(frontendId, backendId)
      expect(resolveBackendSessionId(frontendId)).toBe(backendId)

      // Verify it's in localStorage (per-alias key)
      expect(localStorage.getItem(aliasKey(frontendId))).toBe(backendId)

      // Clear the in-memory map (simulates module reload) - note that
      // clearAllSessionIdMappings now clears all per-alias keys too, so we
      // need to manually restore the storage state
      clearAllSessionIdMappings()
      // Manually restore the alias directly (simulating it surviving the reload)
      localStorage.setItem(aliasKey(frontendId), backendId)

      // Re-hydrate from localStorage (simulates the module's hydrate on load)
      rehydrateFromLocalStorageForTesting()

      // After reload simulation, the alias should still resolve
      expect(resolveBackendSessionId(frontendId)).toBe(backendId)
    })

    it('does not write to localStorage when identical IDs are registered', () => {
      const frontendId = 'fe-no-difference'

      registerBackendSessionId(frontendId, frontendId)

      // Should not create a storage entry when IDs are identical
      expect(localStorage.getItem(aliasKey(frontendId))).toBeNull()
    })

    it('does not write to localStorage when unregistering non-existent mapping', () => {
      const frontendId = 'fe-never-registered'

      // Pre-populate with one entry
      registerBackendSessionId('other-fe', 'other-be')
      const beforeCount = Object.keys(readAllStoredAliases()).length

      // Unregister something that doesn't exist
      unregisterBackendSessionId(frontendId)

      // With per-alias keys, the existing entry should remain, no new entries
      const after = readAllStoredAliases()
      expect(Object.keys(after)).toHaveLength(beforeCount)
      expect(after['other-fe']).toBe('other-be') // Original entry preserved
    })

    it('survives canonicalization after simulated reload', () => {
      const frontendId = 'fe-canonical-reload'
      const backendId = 'be-canonical-reload'

      // Register and persist
      registerBackendSessionId(frontendId, backendId)

      // Simulate reload: manually restore the key since clearAll removes all
      clearAllSessionIdMappings()
      localStorage.setItem(aliasKey(frontendId), backendId)
      rehydrateFromLocalStorageForTesting()

      // Verify canonicalization still works after "reload"
      expect(canonicalizeSessionId(backendId)).toBe(frontendId)
      expect(canonicalizeSessionId(frontendId)).toBe(frontendId)
    })

    it('migrates legacy blob aliases and preserves canonicalization', () => {
      const backendLegacyAlias = {
        'fe-legacy': 'be-legacy',
        'legacy-empty': '',
        invalid: null,
      }

      localStorage.setItem(LEGACY_ALIAS_KEY, JSON.stringify(backendLegacyAlias))

      rehydrateFromLocalStorageForTesting()

      // Legacy key should be removed after migration
      expect(localStorage.getItem(LEGACY_ALIAS_KEY)).toBeNull()

      // Valid legacy entry is hydrated and persisted as per-alias storage
      expect(resolveBackendSessionId('fe-legacy')).toBe('be-legacy')
      expect(localStorage.getItem(aliasKey('fe-legacy'))).toBe('be-legacy')

      // Canonicalization should work from backend -> frontend IDs after migration
      expect(canonicalizeSessionId('be-legacy')).toBe('fe-legacy')

      // Invalid legacy values should be ignored
      expect(hasBackendSessionMapping('legacy-empty')).toBe(false)
      expect(hasBackendSessionMapping('invalid')).toBe(false)
    })

    it('prefers an existing per-alias key over stale legacy blob data', () => {
      localStorage.setItem(aliasKey('fe-legacy'), 'be-newer')

      localStorage.setItem(
        LEGACY_ALIAS_KEY,
        JSON.stringify({ 'fe-legacy': 'be-stale', 'fe-only-legacy': 'be-only' })
      )

      rehydrateFromLocalStorageForTesting()

      expect(resolveBackendSessionId('fe-legacy')).toBe('be-newer')
      expect(localStorage.getItem(aliasKey('fe-legacy'))).toBe('be-newer')

      // Legacy-only entries are migrated to per-alias keys.
      expect(resolveBackendSessionId('fe-only-legacy')).toBe('be-only')
      expect(localStorage.getItem(aliasKey('fe-only-legacy'))).toBe('be-only')

      expect(localStorage.getItem(LEGACY_ALIAS_KEY)).toBeNull()
    })

    it('retains legacy blob when per-alias migration cannot be persisted', () => {
      const originalSetItem = Storage.prototype.setItem
      const writeError = new Error('quota exceeded')
      const setItemSpy = vi
        .spyOn(Storage.prototype as { setItem: typeof localStorage.setItem }, 'setItem')
        .mockImplementation((key: string, value: string) => {
          if (key.startsWith(ALIAS_KEY_PREFIX)) {
            throw writeError
          }
          return originalSetItem.call(localStorage, key, value)
        })

      try {
        const legacyAlias = { 'fe-migrate': 'be-migrate' }
        const serializedLegacy = JSON.stringify(legacyAlias)

        localStorage.setItem(LEGACY_ALIAS_KEY, serializedLegacy)

        rehydrateFromLocalStorageForTesting()

        expect(localStorage.getItem(LEGACY_ALIAS_KEY)).toBe(serializedLegacy)
        expect(resolveBackendSessionId('fe-migrate')).toBe('be-migrate')
      } finally {
        setItemSpy.mockRestore()
      }
    })

    it('removes persisted alias from localStorage on delete cleanup', () => {
      const frontendId = 'fe-delete-cleanup'
      const backendId = 'be-delete-cleanup'

      // Register an alias - it should be in localStorage
      registerBackendSessionId(frontendId, backendId)
      expect(localStorage.getItem(aliasKey(frontendId))).toBe(backendId)

      // Simulate delete cleanup (unregister)
      unregisterBackendSessionId(frontendId)

      // The alias should be removed from localStorage
      expect(localStorage.getItem(aliasKey(frontendId))).toBeNull()
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

      // Verify all are persisted
      const allAliases = readAllStoredAliases()
      expect(Object.keys(allAliases)).toHaveLength(3)
      expect(allAliases['fe-1']).toBe('be-1')
      expect(allAliases['fe-2']).toBe('be-2')
      expect(allAliases['fe-3']).toBe('be-3')

      // Delete only one
      unregisterBackendSessionId('fe-2')

      // Verify others remain in localStorage
      const remaining = readAllStoredAliases()
      expect(remaining['fe-1']).toBe('be-1')
      expect(remaining['fe-2']).toBeUndefined()
      expect(remaining['fe-3']).toBe('be-3')
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
      expect(Object.keys(readAllStoredAliases())).toHaveLength(2)

      // Clear all
      clearAllSessionIdMappings()

      // localStorage should be empty
      expect(readAllStoredAliases()).toEqual({})
      expect(getSessionMappingCount()).toBe(0)
    })

    it('handles reload with malformed keys gracefully', () => {
      // Per-alias storage doesn't use JSON, so "corrupted JSON" case doesn't apply
      // but we test that unexpected/malformed keys are ignored during hydration
      localStorage.setItem('ava_sa:valid', 'backend-valid')
      localStorage.setItem('ava_sa:', '') // Empty frontendId (should be ignored)
      localStorage.setItem('ava_sa:no-value', '') // Empty value (should be ignored)

      // Rehydrate should not throw
      expect(() => rehydrateFromLocalStorageForTesting()).not.toThrow()

      // Valid alias should be loaded
      expect(resolveBackendSessionId('valid')).toBe('backend-valid')
      // Empty/malformed keys should be ignored
      expect(hasBackendSessionMapping('')).toBe(false)
      expect(hasBackendSessionMapping('no-value')).toBe(false)
      expect(getSessionMappingCount()).toBe(1) // Only 'valid'
    })

    it('handles empty storage values gracefully', () => {
      // Per-alias storage might have empty values if something goes wrong
      localStorage.setItem('ava_sa:empty-test', '')

      // Rehydrate should not throw
      expect(() => rehydrateFromLocalStorageForTesting()).not.toThrow()

      // Empty values should be ignored
      expect(hasBackendSessionMapping('empty-test')).toBe(false)
    })
  })

  describe('multi-tab safety (cross-instance synchronization)', () => {
    it('independent per-alias writes do not clobber each other', () => {
      // With per-alias keys, each write is independent
      // Simulate Tab A having already registered an alias
      localStorage.setItem(aliasKey('tab-a-session'), 'tab-a-backend')

      // Simulate Tab B registering a different alias
      // Tab B's write only touches its own key
      registerBackendSessionId('tab-b-session', 'tab-b-backend')

      // Both aliases should now be in localStorage (independent keys)
      expect(localStorage.getItem(aliasKey('tab-a-session'))).toBe('tab-a-backend')
      expect(localStorage.getItem(aliasKey('tab-b-session'))).toBe('tab-b-backend')

      // Tab B should be able to resolve both after rehydration
      rehydrateFromLocalStorageForTesting()
      expect(resolveBackendSessionId('tab-a-session')).toBe('tab-a-backend')
      expect(resolveBackendSessionId('tab-b-session')).toBe('tab-b-backend')
    })

    it('allows two independent module instances to write aliases without clobbering', () => {
      // With per-alias keys, each write is independent
      // Tab A registers first
      localStorage.setItem(aliasKey('instance-a-session'), 'instance-a-backend')

      // Tab B comes online, hydrates from storage, then registers its own alias
      rehydrateFromLocalStorageForTesting()
      expect(hasBackendSessionMapping('instance-a-session')).toBe(true)

      // Tab B registers its alias (only writes its own key)
      registerBackendSessionId('instance-b-session', 'instance-b-backend')

      // Verify both exist independently
      expect(localStorage.getItem(aliasKey('instance-a-session'))).toBe('instance-a-backend')
      expect(localStorage.getItem(aliasKey('instance-b-session'))).toBe('instance-b-backend')
    })

    it('deletes are isolated and do not affect other aliases', () => {
      // With per-alias keys, delete only removes one key
      localStorage.setItem(aliasKey('keep-session'), 'keep-backend')
      localStorage.setItem(aliasKey('delete-session'), 'delete-backend')

      // Hydrate both
      rehydrateFromLocalStorageForTesting()
      expect(hasBackendSessionMapping('keep-session')).toBe(true)
      expect(hasBackendSessionMapping('delete-session')).toBe(true)

      // Delete one
      unregisterBackendSessionId('delete-session')

      // Verify only the intended one was deleted (other key untouched)
      expect(localStorage.getItem(aliasKey('keep-session'))).toBe('keep-backend')
      expect(localStorage.getItem(aliasKey('delete-session'))).toBeNull()
    })

    it('concurrent writes to different keys are naturally isolated', () => {
      // With per-alias keys, concurrent writes to different keys cannot interfere
      localStorage.setItem(aliasKey('alias-1'), 'backend-1-original')

      // Tab B updates alias-1 (simulated by direct write)
      localStorage.setItem(aliasKey('alias-1'), 'backend-1-updated')
      localStorage.setItem(aliasKey('alias-2'), 'backend-2')

      // Tab A registers alias-3 (only touches its own key)
      registerBackendSessionId('alias-3', 'backend-3')

      // All three should exist with correct values (no clobbering possible)
      expect(localStorage.getItem(aliasKey('alias-1'))).toBe('backend-1-updated')
      expect(localStorage.getItem(aliasKey('alias-2'))).toBe('backend-2')
      expect(localStorage.getItem(aliasKey('alias-3'))).toBe('backend-3')
    })

    it('survives race condition where storage is modified during register', () => {
      // With per-alias keys, races are isolated to individual keys
      // Start with an existing alias
      registerBackendSessionId('existing-session', 'existing-backend')

      // Another tab adds a different alias (different key)
      localStorage.setItem(aliasKey('concurrent-session'), 'concurrent-backend')

      // Now our tab registers another alias (only touches its key)
      registerBackendSessionId('new-session', 'new-backend')

      // All three should exist (keys are independent)
      expect(localStorage.getItem(aliasKey('existing-session'))).toBe('existing-backend')
      expect(localStorage.getItem(aliasKey('concurrent-session'))).toBe('concurrent-backend')
      expect(localStorage.getItem(aliasKey('new-session'))).toBe('new-backend')
    })

    it('does not resurrect aliases deleted in another tab (delete-wins race)', () => {
      // Tab A has two aliases registered
      registerBackendSessionId('keep-alive', 'backend-1')
      registerBackendSessionId('to-be-deleted', 'backend-2')

      // Verify both are in storage (as separate keys)
      expect(localStorage.getItem(aliasKey('keep-alive'))).toBe('backend-1')
      expect(localStorage.getItem(aliasKey('to-be-deleted'))).toBe('backend-2')

      // Simulate Tab B deleting 'to-be-deleted' (removes only that key)
      localStorage.removeItem(aliasKey('to-be-deleted'))

      // Tab A (this instance) still has 'to-be-deleted' in memory
      // because it hasn't synced yet. Now Tab A registers a new alias.
      // With per-alias keys, register only writes its own key.
      registerBackendSessionId('new-alias', 'backend-3')

      // Check that 'to-be-deleted' was NOT resurrected (key still removed)
      expect(localStorage.getItem(aliasKey('keep-alive'))).toBe('backend-1')
      expect(localStorage.getItem(aliasKey('to-be-deleted'))).toBeNull() // Still deleted!
      expect(localStorage.getItem(aliasKey('new-alias'))).toBe('backend-3')
    })

    it('does not replay stale in-memory aliases on registration', () => {
      // Start with an alias
      registerBackendSessionId('stale-alias', 'stale-backend')

      // Another tab deletes it (removes the key)
      localStorage.removeItem(aliasKey('stale-alias'))

      // This tab still has stale-alias in memory (hasn't synced)
      expect(hasBackendSessionMapping('stale-alias')).toBe(true)

      // Register a new alias - with per-alias keys, only writes new key
      registerBackendSessionId('fresh-alias', 'fresh-backend')

      // Stale alias should remain deleted (its key was never recreated)
      expect(localStorage.getItem(aliasKey('stale-alias'))).toBeNull()
      expect(localStorage.getItem(aliasKey('fresh-alias'))).toBe('fresh-backend')
    })

    it('stale tab can delete storage-only alias not in memory', () => {
      // Storage has an alias that this tab doesn't have in memory
      // (e.g., tab opened before the alias was created, or hasn't synced)
      localStorage.setItem(aliasKey('storage-only'), 'backend-id')

      // Verify memory does NOT have the alias (stale tab state)
      expect(hasBackendSessionMapping('storage-only')).toBe(false)

      // Unregister should still delete from storage (removes the key)
      unregisterBackendSessionId('storage-only')

      // Verify alias is removed from storage
      expect(localStorage.getItem(aliasKey('storage-only'))).toBeNull()
      expect(readAllStoredAliases()).toEqual({})
    })
  })

  describe('storage event synchronization', () => {
    it('syncs added alias from storage event', () => {
      // Start empty
      expect(getSessionMappingCount()).toBe(0)

      // Simulate storage event from another tab adding an alias
      const storageEvent = new StorageEvent('storage', {
        key: aliasKey('remote-session'),
        newValue: 'remote-backend',
        oldValue: null,
        storageArea: localStorage,
      })
      window.dispatchEvent(storageEvent)

      // Should now have the remote alias
      expect(hasBackendSessionMapping('remote-session')).toBe(true)
      expect(resolveBackendSessionId('remote-session')).toBe('remote-backend')
    })

    it('syncs updated alias from storage event', () => {
      // Start with an existing alias
      registerBackendSessionId('shared-session', 'old-backend')
      expect(resolveBackendSessionId('shared-session')).toBe('old-backend')

      // Simulate storage event from another tab updating the alias
      const storageEvent = new StorageEvent('storage', {
        key: aliasKey('shared-session'),
        newValue: 'new-backend',
        oldValue: 'old-backend',
        storageArea: localStorage,
      })
      window.dispatchEvent(storageEvent)

      // Should have the updated backend ID
      expect(resolveBackendSessionId('shared-session')).toBe('new-backend')
    })

    it('syncs deleted alias from storage event', () => {
      // Start with two aliases
      registerBackendSessionId('keep-session', 'backend-1')
      registerBackendSessionId('delete-session', 'backend-2')
      expect(getSessionMappingCount()).toBe(2)

      // Simulate storage event from another tab deleting one alias (null value)
      const storageEvent = new StorageEvent('storage', {
        key: aliasKey('delete-session'),
        newValue: null,
        oldValue: 'backend-2',
        storageArea: localStorage,
      })
      window.dispatchEvent(storageEvent)

      // Should only have the kept alias
      expect(hasBackendSessionMapping('keep-session')).toBe(true)
      expect(hasBackendSessionMapping('delete-session')).toBe(false)
      expect(getSessionMappingCount()).toBe(1)
    })

    it('ignores storage events for other keys', () => {
      // Start with an alias
      registerBackendSessionId('my-session', 'my-backend')

      // Storage event for a different key
      const storageEvent = new StorageEvent('storage', {
        key: 'some_other_key',
        newValue: 'other-backend',
        oldValue: null,
        storageArea: localStorage,
      })
      window.dispatchEvent(storageEvent)

      // Should not be affected
      expect(getSessionMappingCount()).toBe(1)
      expect(hasBackendSessionMapping('my-session')).toBe(true)
      expect(hasBackendSessionMapping('other-session')).toBe(false)
    })
  })
})
