/**
 * Web Database Fallback
 *
 * When running in a browser (non-Tauri), the SQL plugin is unavailable.
 * This module provides a fake Database-like object that routes session and
 * message operations through the AVA HTTP API served by `ava serve`.
 *
 * Only the SQL patterns actually used by db-sessions.ts and db-messages.ts
 * are intercepted. Other queries return empty results (safe for agents,
 * resources, checkpoints, stats which are non-critical for basic chat).
 */

import { apiFetch, buildApiUrl } from '../lib/api-client'
import { mapWebSessionMessageRows } from '../lib/web-session-messages'
import {
  buildSessionEndpoint,
  canonicalizeSessionId,
  resolveBackendSessionId,
} from './web-session-identity'
import { writeBrowserSession, writeBrowserSessionCollection } from './web-session-write-client'

function buildWriteFailureMessage(action: string, detail: string): string {
  return `[db-web] Failed to ${action}: ${detail}`
}

function getWriteFailureDetail(result: {
  errorText?: string
  status: number
  statusText: string
}): string {
  return result.errorText || `${result.status} ${result.statusText}`
}

async function requireSuccessfulSessionWrite(
  action: string,
  writePromise: Promise<{ ok: boolean; errorText?: string; status: number; statusText: string }>
): Promise<void> {
  const result = await writePromise
  if (result.ok) {
    return
  }

  const detail = getWriteFailureDetail(result)
  console.warn(`[db-web] Failed to ${action}:`, detail)
  throw new Error(buildWriteFailureMessage(action, detail))
}

function buildSessionListEndpoint(query: string, params?: unknown[]): string {
  const archivedOnly = query.includes("where s.status = 'archived'")
  const status = archivedOnly ? 'archived' : 'active'
  const searchParams = new URLSearchParams({ status })
  const projectId = query.includes('s.project_id = ?')
    ? ((params ?? [])[0] as string | null | undefined) || undefined
    : undefined

  if (projectId) {
    searchParams.set('project_id', projectId)
  }

  return buildApiUrl(`/api/sessions?${searchParams.toString()}`)
}

interface ExecuteResult {
  rowsAffected: number
  lastInsertId?: number
}

interface WebDatabase {
  select<T>(query: string, params?: unknown[]): Promise<T>
  execute(query: string, params?: unknown[]): Promise<ExecuteResult>
}

// Session ID aliasing is now handled by web-session-identity.ts
// This file no longer owns the mapping logic — it only resolves through that service.

/**
 * Create a web-mode database adapter that routes operations through HTTP.
 */
export function createWebDatabase(): WebDatabase {
  return {
    select: async <T>(query: string, _params?: unknown[]): Promise<T> => {
      const q = query.trim().toLowerCase()

      // Session listing queries
      if (q.includes('from sessions s') && q.includes('left join messages m')) {
        // getSessionsWithStats / getArchivedSessions
        try {
          const res = await apiFetch(buildSessionListEndpoint(q, _params))
          if (!res.ok) {
            console.warn('[db-web] Failed to list sessions:', res.status, res.statusText)
            return [] as unknown as T
          }
          const requestedProjectId = q.includes('s.project_id = ?')
            ? ((_params ?? [])[0] as string | null | undefined) || undefined
            : undefined
          const sessions = await res.json()
          // Map API response to the row format expected by mapSessionRow.
          // The backend may return fields as either snake_case or camelCase;
          // also handle `name` vs `title` variations.
          // CRITICAL: Canonicalize session IDs - backend may return backend IDs,
          // but frontend state must always use the canonical frontend session ID.
          const canonicalizeParentId = (value: unknown): string | null => {
            if (typeof value !== 'string' || !value) return null
            return canonicalizeSessionId(value)
          }
          return (Array.isArray(sessions) ? sessions : [])
            .filter((session: Record<string, unknown>) => {
              if (!requestedProjectId) {
                return true
              }

              const sessionProjectId = session.project_id ?? session.projectId ?? null
              return sessionProjectId === requestedProjectId
            })
            .map((s: Record<string, unknown>) => ({
              id: canonicalizeSessionId(s.id as string),
              name: s.title || s.name || 'Untitled',
              project_id: s.project_id ?? s.projectId ?? null,
              parent_session_id: canonicalizeParentId(s.parent_session_id ?? s.parentSessionId),
              slug: s.slug ?? null,
              busy_since: s.busy_since ?? null,
              created_at:
                typeof s.created_at === 'number'
                  ? s.created_at
                  : typeof s.created_at === 'string'
                    ? new Date(s.created_at).getTime()
                    : Date.now(),
              updated_at:
                typeof s.updated_at === 'number'
                  ? s.updated_at
                  : typeof s.updated_at === 'string'
                    ? new Date(s.updated_at).getTime()
                    : Date.now(),
              status: s.status ?? 'active',
              metadata: s.metadata ?? null,
              message_count: s.message_count ?? 0,
              total_tokens: s.total_tokens ?? 0,
              total_cost: s.total_cost ?? 0,
              last_preview: s.last_preview ?? null,
            })) as unknown as T
        } catch (e) {
          console.warn('[db-web] Failed to list sessions:', e)
          return [] as unknown as T
        }
      }

      // Single-message metadata fetch (used by updateMessage to merge metadata before UPDATE)
      // SQL: SELECT metadata FROM messages WHERE id = ?
      // In web mode we have no per-message metadata store, so return an empty row.
      // updateMessage will treat this as no existing metadata (merge from empty object).
      if (q.includes('from messages') && q.includes('where id')) {
        return [] as unknown as T
      }

      // Message listing for a session
      if (q.includes('from messages') && q.includes('session_id')) {
        const params = _params ?? []
        const frontendSessionId = params[0] as string
        if (!frontendSessionId) return [] as unknown as T
        // Use the backend session ID if we have a mapping (frontend and backend IDs diverge)
        const sessionId = resolveBackendSessionId(frontendSessionId)
        try {
          // Use the dedicated messages endpoint for a flat array of MessageSummary objects.
          // Fall back to the session detail endpoint if the dedicated endpoint is unavailable.
          let msgs: Record<string, unknown>[] = []
          const msgsRes = await apiFetch(`/api/sessions/${sessionId}/messages`)
          if (msgsRes.ok) {
            const data = await msgsRes.json()
            msgs = Array.isArray(data) ? data : []
          } else if (msgsRes.status === 404) {
            // Older server: fall back to session detail endpoint
            const detailRes = await apiFetch(`/api/sessions/${sessionId}`)
            if (detailRes.ok) {
              const detail = await detailRes.json()
              msgs = Array.isArray(detail.messages)
                ? (detail.messages as Record<string, unknown>[])
                : []
            } else {
              console.warn('[db-web] Failed to load session messages:', detailRes.status, sessionId)
              return [] as unknown as T
            }
          } else {
            console.warn('[db-web] Failed to load session messages:', msgsRes.status, sessionId)
            return [] as unknown as T
          }
          return mapWebSessionMessageRows(msgs, frontendSessionId) as unknown as T
        } catch (e) {
          console.warn('[db-web] Failed to load session messages:', e)
          return [] as unknown as T
        }
      }

      // Schema version check
      if (q.includes('sqlite_master') || q.includes('schema_version')) {
        return [] as unknown as T
      }

      // Session sub-resource tables — route through HTTP API stubs
      // All operations resolve frontend→backend session IDs at the adapter boundary
      if (q.includes('from agents') && q.includes('session_id')) {
        const frontendSessionId = (_params ?? [])[0] as string
        if (frontendSessionId) {
          try {
            const res = await apiFetch(buildSessionEndpoint(frontendSessionId, 'agents'))
            if (res.ok) return (await res.json()) as T
          } catch {
            /* fall through */
          }
        }
        return [] as unknown as T
      }

      if (q.includes('from file_operations') && q.includes('session_id')) {
        const frontendSessionId = (_params ?? [])[0] as string
        if (frontendSessionId) {
          try {
            const res = await apiFetch(buildSessionEndpoint(frontendSessionId, 'files'))
            if (res.ok) return (await res.json()) as T
          } catch {
            /* fall through */
          }
        }
        return [] as unknown as T
      }

      if (q.includes('from terminal_executions') && q.includes('session_id')) {
        const frontendSessionId = (_params ?? [])[0] as string
        if (frontendSessionId) {
          try {
            const res = await apiFetch(buildSessionEndpoint(frontendSessionId, 'terminal'))
            if (res.ok) return (await res.json()) as T
          } catch {
            /* fall through */
          }
        }
        return [] as unknown as T
      }

      if (q.includes('from memory_items') && q.includes('session_id')) {
        const frontendSessionId = (_params ?? [])[0] as string
        if (frontendSessionId) {
          // Checkpoints are memory_items with type='checkpoint'
          const isCheckpoint = q.includes("type = 'checkpoint'") || q.includes('type = ?')
          const path = isCheckpoint ? 'checkpoints' : 'memory'
          try {
            const res = await apiFetch(buildSessionEndpoint(frontendSessionId, path))
            if (res.ok) return (await res.json()) as T
          } catch {
            /* fall through */
          }
        }
        return [] as unknown as T
      }

      // Non-critical tables without session_id context — return empty silently
      if (
        q.includes('from agents') ||
        q.includes('from file_operations') ||
        q.includes('from terminal_executions') ||
        q.includes('from memory_items')
      ) {
        return [] as unknown as T
      }

      // Default: return empty array for any other select
      if (import.meta.env.DEV) {
        console.warn('[db-web] Unhandled SELECT query (returning []):', q.slice(0, 80))
      }
      return [] as unknown as T
    },

    execute: async (query: string, params?: unknown[]): Promise<ExecuteResult> => {
      const q = query.trim().toLowerCase()

      // Create session
      // Params: [id, name, projectId, parentSessionId, createdAt, updatedAt, status]
      if (q.includes('insert into sessions')) {
        const id = params?.[0] as string
        const name = (params?.[1] as string) || 'New Session'
        const projectId = ((params?.[2] as string | null | undefined) ?? null) || undefined
        if (!id) {
          throw new Error(buildWriteFailureMessage('create session', 'missing frontend session ID'))
        }

        await requireSuccessfulSessionWrite(
          'create session',
          writeBrowserSessionCollection({
            action: 'create',
            method: 'POST',
            jsonBody: { name, id, project_id: projectId },
          })
        )

        return { rowsAffected: 1 }
      }

      // Update session (rename, status change, etc.)
      if (q.includes('update sessions set')) {
        // The last param is the session ID (WHERE id = ?)
        const frontendSessionId = params?.[params.length - 1] as string
        if (frontendSessionId) {
          let valueIndex = 1 // params[0] is updated_at
          const name = q.includes('name =')
            ? (params?.[valueIndex++] as string | undefined)
            : undefined
          const status = q.includes('status =')
            ? (params?.[valueIndex++] as string | undefined)
            : undefined

          if (!frontendSessionId) {
            throw new Error(
              buildWriteFailureMessage('update session', 'missing frontend session ID')
            )
          }

          if (name) {
            await requireSuccessfulSessionWrite(
              'rename session',
              writeBrowserSession({
                frontendSessionId,
                action: 'rename',
                method: 'POST',
                jsonBody: { name },
              })
            )
          }

          if (status === 'archived' || status === 'active') {
            const action = status === 'archived' ? 'archive' : 'unarchive'
            await requireSuccessfulSessionWrite(
              `${action} session`,
              writeBrowserSession({
                frontendSessionId,
                action,
                method: 'POST',
              })
            )
          }
        }
        // updated_at-only (touchSession), metadata-only, etc. remain no-ops in web mode
        return { rowsAffected: 1 }
      }

      // Delete session
      if (q.includes('delete from sessions')) {
        const frontendSessionId = params?.[0] as string
        if (!frontendSessionId) {
          throw new Error(buildWriteFailureMessage('delete session', 'missing frontend session ID'))
        }

        await requireSuccessfulSessionWrite(
          'delete session',
          writeBrowserSession({
            frontendSessionId,
            method: 'DELETE',
          })
        )

        return { rowsAffected: 1 }
      }

      // Insert message — no-op in web mode.
      // The Rust agent is the single writer for message persistence; the frontend
      // fetches the authoritative list from the backend API after each run.
      if (q.includes('insert into messages')) {
        return { rowsAffected: 1 }
      }

      // Update message — no-op in web mode (same reason as above).
      if (q.includes('update messages set')) {
        return { rowsAffected: 1 }
      }

      // Delete messages
      if (q.includes('delete from messages')) {
        // In web mode, messages are managed server-side.
        return { rowsAffected: 0 }
      }

      // Schema version / migration DDL — no-op in web mode
      if (
        q.includes('create table') ||
        q.includes('create index') ||
        q.includes('insert into schema_version') ||
        q.includes('alter table')
      ) {
        return { rowsAffected: 0 }
      }

      // Default: no-op
      if (import.meta.env.DEV) {
        console.warn('[db-web] Unhandled EXECUTE query (no-op):', q.slice(0, 80))
      }
      return { rowsAffected: 0 }
    },
  }
}
