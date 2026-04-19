/**
 * Web Session Identity Service
 *
 * Manages frontend→backend session alias registration and resolution for web mode.
 * This is a dedicated service that owns session ID mapping concerns, separate from
 * the database fallback layer and the session store.
 *
 * The session store remains keyed by frontend session ID; this service provides
 * the mapping layer when backend session IDs diverge (e.g., after retry/regenerate
 * operations that create new backend sessions).
 *
 * Reload Persistence:
 * Session alias mappings are persisted to localStorage using per-alias keys
 * (ava_sa:${frontendId}) to avoid whole-blob read/modify/write races.
 * Each alias is stored independently, so unrelated aliases cannot clobber each other
 * under cross-tab interleaving. Mappings are cleaned up only when a session is
 * permanently deleted.
 */

const API_BASE = import.meta.env.VITE_API_URL || ''

/** localStorage key prefix for per-alias storage. */
const ALIAS_KEY_PREFIX = 'ava_sa:'

/** legacy storage key for the pre-refactor blob-backed alias map. */
const LEGACY_ALIAS_KEY = 'ava_session_id_aliases'

/**
 * Maps frontend session IDs to backend session IDs.
 *
 * With the session ID pass-through fix, submit_goal now uses the frontend's
 * session ID so IDs should always match. This map is kept as a fallback for
 * edge cases (e.g., retry/regenerate creating new sessions) but should
 * rarely be populated.
 *
 * This map is automatically hydrated from localStorage on module load and
 * stays synchronized across tabs via the storage event.
 */
const _sessionIdMap = new Map<string, string>()

/**
 * Build the localStorage key for a specific alias.
 */
function aliasStorageKey(frontendId: string): string {
  return `${ALIAS_KEY_PREFIX}${frontendId}`
}

/**
 * Extract frontendId from a storage key.
 */
function parseAliasStorageKey(key: string): string | null {
  if (!key.startsWith(ALIAS_KEY_PREFIX)) return null
  return key.slice(ALIAS_KEY_PREFIX.length)
}

/**
 * Persist a single alias registration to storage.
 * Uses independent per-alias keys to avoid whole-blob races.
 * @returns true if the value was written successfully
 */
function persistAliasRegistration(frontendId: string, backendId: string): boolean {
  try {
    localStorage.setItem(aliasStorageKey(frontendId), backendId)
    return true
  } catch {
    // Silently fail if localStorage is unavailable or full
    return false
  }
}

/**
 * Remove a specific alias from storage.
 * Uses independent per-alias keys to avoid whole-blob races.
 */
function persistSessionIdDeletion(frontendId: string): void {
  try {
    localStorage.removeItem(aliasStorageKey(frontendId))
  } catch {
    // Silently fail if localStorage is unavailable
  }
}

/**
 * Hydrate session ID mappings from localStorage.
 * Called once on module load to restore aliases across reloads.
 */
function hydrateSessionIdMappings(): void {
  try {
    // Iterate all localStorage keys and find those matching our prefix
    for (let i = 0; i < localStorage.length; i++) {
      const key = localStorage.key(i)
      if (!key || !key.startsWith(ALIAS_KEY_PREFIX)) continue

      const frontendId = parseAliasStorageKey(key)
      if (!frontendId) continue

      const backendId = localStorage.getItem(key)
      if (backendId) {
        _sessionIdMap.set(frontendId, backendId)
      }
    }

    // One-time migration from legacy blob format to per-alias keys.
    const rawLegacyAliasMap = localStorage.getItem(LEGACY_ALIAS_KEY)
    if (rawLegacyAliasMap !== null) {
      let legacyMigrationSucceeded = true
      try {
        const parsed: unknown = JSON.parse(rawLegacyAliasMap)
        if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) {
          for (const [frontendId, backendId] of Object.entries(parsed as Record<string, unknown>)) {
            if (typeof frontendId !== 'string' || !frontendId) continue
            if (typeof backendId !== 'string' || !backendId) continue

            if (!_sessionIdMap.has(frontendId)) {
              _sessionIdMap.set(frontendId, backendId)

              const migrated = persistAliasRegistration(frontendId, backendId)
              if (!migrated) {
                legacyMigrationSucceeded = false
              }
            }
          }
        } else {
          // Non-object payload is not a recognized legacy shape.
          legacyMigrationSucceeded = false
        }
      } catch {
        // Silently ignore malformed legacy payloads
        legacyMigrationSucceeded = false
      } finally {
        // Remove legacy key only after a successful migration.
        // If migration did not fully persist, keep legacy key for retry.
        if (legacyMigrationSucceeded) {
          localStorage.removeItem(LEGACY_ALIAS_KEY)
        }
      }
    }
  } catch {
    // Silently fail if localStorage is unavailable
  }
}

/**
 * Listen for storage events from other tabs to keep aliases synchronized.
 * With per-alias keys, each alias change is independent and cannot race.
 */
if (typeof window !== 'undefined') {
  window.addEventListener('storage', (event) => {
    if (!event.key || !event.key.startsWith(ALIAS_KEY_PREFIX)) return

    const frontendId = parseAliasStorageKey(event.key)
    if (!frontendId) return

    if (event.newValue === null) {
      // Alias was deleted in another tab
      _sessionIdMap.delete(frontendId)
    } else {
      // Alias was added or updated in another tab
      _sessionIdMap.set(frontendId, event.newValue)
    }
  })
}

// Hydrate on module load
hydrateSessionIdMappings()

/**
 * Re-hydrate session ID mappings from localStorage.
 * This is exported for testing purposes to simulate a browser reload.
 * @internal - not for production use
 */
export function rehydrateFromLocalStorageForTesting(): void {
  _sessionIdMap.clear()
  hydrateSessionIdMappings()
}

/** Register a frontend→backend session ID mapping (called after agent run). */
export function registerBackendSessionId(frontendId: string, backendId: string): void {
  if (frontendId !== backendId) {
    _sessionIdMap.set(frontendId, backendId)
    persistAliasRegistration(frontendId, backendId)
  }
}

/** Resolve a backend session ID from a frontend session ID. Returns the frontend ID if no mapping exists. */
export function resolveBackendSessionId(frontendId: string): string {
  return _sessionIdMap.get(frontendId) || frontendId
}

/** Check if a mapping exists for the given frontend session ID. */
export function hasBackendSessionMapping(frontendId: string): boolean {
  return _sessionIdMap.has(frontendId)
}

/** Remove a session ID mapping (e.g., when a session is deleted). */
export function unregisterBackendSessionId(frontendId: string): void {
  // Always delete from memory (idempotent) and always persist the deletion.
  // This ensures a stale tab (without the alias in memory) can still remove
  // a storage-only alias left by another tab.
  _sessionIdMap.delete(frontendId)
  persistSessionIdDeletion(frontendId)
}

/** Clear all session ID mappings (primarily for testing). */
export function clearAllSessionIdMappings(): void {
  _sessionIdMap.clear()
  try {
    // Remove all keys with our prefix
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
  } catch {
    // Silently fail if localStorage is unavailable
  }
}

/** Get the raw map size for debugging/inspection. */
export function getSessionMappingCount(): number {
  return _sessionIdMap.size
}

/**
 * Build a reverse lookup map from backend session IDs to frontend session IDs.
 * This is needed when the backend returns session data using backend IDs,
 * and we need to canonicalize them back to the frontend IDs.
 */
function buildReverseAliasMap(): Map<string, string> {
  const reverseMap = new Map<string, string>()
  for (const [frontendId, backendId] of _sessionIdMap.entries()) {
    reverseMap.set(backendId, frontendId)
  }
  return reverseMap
}

/**
 * Resolve a frontend session ID from a backend session ID.
 * This is the reverse of resolveBackendSessionId - when the backend returns
 * a session with its ID, we need to map it back to the canonical frontend ID.
 * Returns the backendId unchanged if no reverse mapping exists.
 */
export function resolveFrontendSessionId(backendId: string): string {
  const reverseMap = buildReverseAliasMap()
  return reverseMap.get(backendId) || backendId
}

/**
 * Canonicalize a session ID - convert backend IDs back to frontend IDs.
 * Use this on session data returned from the backend to ensure frontend
 * state remains keyed by frontend session IDs.
 */
export function canonicalizeSessionId(potentialBackendId: string): string {
  return resolveFrontendSessionId(potentialBackendId)
}

/**
 * Build a session-scoped API endpoint URL with automatic alias resolution.
 * This helper ensures all session-scoped web operations resolve the frontend
 * session ID to the backend session ID at the adapter boundary.
 *
 * @param frontendSessionId - The frontend session ID (will be resolved to backend ID if mapped)
 * @param path - The endpoint path (e.g., 'agents', 'files', 'messages')
 * @returns The full API endpoint URL
 */
export function buildSessionEndpoint(frontendSessionId: string, path: string): string {
  const backendSessionId = resolveBackendSessionId(frontendSessionId)
  return `${API_BASE}/api/sessions/${backendSessionId}/${path}`
}

/**
 * Build a session base API endpoint URL with automatic alias resolution.
 * Use this for operations targeting the session itself (rename, delete, etc.)
 *
 * @param frontendSessionId - The frontend session ID (will be resolved to backend ID if mapped)
 * @param suffix - Optional endpoint suffix (e.g., 'rename')
 * @returns The full API endpoint URL
 */
export function buildSessionBaseEndpoint(frontendSessionId: string, suffix?: string): string {
  const backendSessionId = resolveBackendSessionId(frontendSessionId)
  const base = `${API_BASE}/api/sessions/${backendSessionId}`
  return suffix ? `${base}/${suffix}` : base
}
