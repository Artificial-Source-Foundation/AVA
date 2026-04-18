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
 * Session alias mappings are persisted to localStorage and automatically hydrated
 * on module load. This ensures alias mappings survive browser reloads and new tabs.
 * Mappings are cleaned up only when a session is permanently deleted.
 */

import { STORAGE_KEYS } from '../config/constants'

const API_BASE = import.meta.env.VITE_API_URL || ''

/**
 * Maps frontend session IDs to backend session IDs.
 *
 * With the session ID pass-through fix, submit_goal now uses the frontend's
 * session ID so IDs should always match. This map is kept as a fallback for
 * edge cases (e.g., retry/regenerate creating new sessions) but should
 * rarely be populated.
 *
 * This map is automatically hydrated from localStorage on module load.
 */
const _sessionIdMap = new Map<string, string>()

/**
 * Storage shape for persisted alias mappings.
 * Using a Record for JSON serialization stability.
 */
type PersistedAliasMap = Record<string, string>

/**
 * Persist the current session ID mappings to localStorage.
 * Called automatically on register/unregister operations.
 */
function persistSessionIdMappings(): void {
  try {
    const record: PersistedAliasMap = Object.fromEntries(_sessionIdMap.entries())
    localStorage.setItem(STORAGE_KEYS.SESSION_ID_ALIASES, JSON.stringify(record))
  } catch {
    // Silently fail if localStorage is unavailable or full
  }
}

/**
 * Hydrate session ID mappings from localStorage.
 * Called once on module load to restore aliases across reloads.
 */
function hydrateSessionIdMappings(): void {
  try {
    const raw = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
    if (!raw) return

    const parsed: unknown = JSON.parse(raw)
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) return

    const record = parsed as PersistedAliasMap
    for (const [frontendId, backendId] of Object.entries(record)) {
      if (typeof frontendId === 'string' && typeof backendId === 'string') {
        _sessionIdMap.set(frontendId, backendId)
      }
    }
  } catch {
    // Silently fail if localStorage is unavailable or corrupted
  }
}

// Hydrate on module load
hydrateSessionIdMappings()

/** Register a frontend→backend session ID mapping (called after agent run). */
export function registerBackendSessionId(frontendId: string, backendId: string): void {
  if (frontendId !== backendId) {
    _sessionIdMap.set(frontendId, backendId)
    persistSessionIdMappings()
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
  const hadMapping = _sessionIdMap.delete(frontendId)
  if (hadMapping) {
    persistSessionIdMappings()
  }
}

/** Clear all session ID mappings (primarily for testing). */
export function clearAllSessionIdMappings(): void {
  _sessionIdMap.clear()
  try {
    localStorage.removeItem(STORAGE_KEYS.SESSION_ID_ALIASES)
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
