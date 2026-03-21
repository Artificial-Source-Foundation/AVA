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

const API_BASE = import.meta.env.VITE_API_URL || ''

interface ExecuteResult {
  rowsAffected: number
  lastInsertId?: number
}

interface WebDatabase {
  select<T>(query: string, params?: unknown[]): Promise<T>
  execute(query: string, params?: unknown[]): Promise<ExecuteResult>
}

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
          const res = await fetch(`${API_BASE}/api/sessions`)
          if (!res.ok) {
            console.warn('[db-web] Failed to list sessions:', res.status, res.statusText)
            return [] as unknown as T
          }
          const sessions = await res.json()
          // Map API response to the row format expected by mapSessionRow.
          // The backend may return fields as either snake_case or camelCase;
          // also handle `name` vs `title` variations.
          return (Array.isArray(sessions) ? sessions : []).map((s: Record<string, unknown>) => ({
            id: s.id,
            name: s.title || s.name || 'Untitled',
            project_id: s.project_id ?? null,
            parent_session_id: s.parent_session_id ?? null,
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
        const sessionId = params[0] as string
        if (!sessionId) return [] as unknown as T
        try {
          // Use the dedicated messages endpoint for a flat array of MessageSummary objects.
          // Fall back to the session detail endpoint if the dedicated endpoint is unavailable.
          let msgs: Record<string, unknown>[] = []
          const msgsRes = await fetch(`${API_BASE}/api/sessions/${sessionId}/messages`)
          if (msgsRes.ok) {
            const data = await msgsRes.json()
            msgs = Array.isArray(data) ? data : []
          } else if (msgsRes.status === 404) {
            // Older server: fall back to session detail endpoint
            const detailRes = await fetch(`${API_BASE}/api/sessions/${sessionId}`)
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
          // Map to db row format expected by mapDbMessages.
          // Handle both `timestamp` and `created_at` field names.
          return msgs.map((m: Record<string, unknown>) => ({
            id: m.id,
            session_id: sessionId,
            role: m.role,
            content: m.content,
            agent_id: m.agent_id ?? null,
            created_at:
              typeof m.created_at === 'number'
                ? m.created_at
                : typeof m.timestamp === 'string'
                  ? new Date(m.timestamp).getTime()
                  : typeof m.created_at === 'string'
                    ? new Date(m.created_at).getTime()
                    : Date.now(),
            tokens_used: m.tokens_used ?? 0,
            cost_usd: m.cost_usd ?? null,
            model: m.model ?? null,
            metadata: m.metadata ?? '{}',
          })) as unknown as T
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
      if (q.includes('from agents') && q.includes('session_id')) {
        const sessionId = (_params ?? [])[0] as string
        if (sessionId) {
          try {
            const res = await fetch(`${API_BASE}/api/sessions/${sessionId}/agents`)
            if (res.ok) return (await res.json()) as T
          } catch {
            /* fall through */
          }
        }
        return [] as unknown as T
      }

      if (q.includes('from file_operations') && q.includes('session_id')) {
        const sessionId = (_params ?? [])[0] as string
        if (sessionId) {
          try {
            const res = await fetch(`${API_BASE}/api/sessions/${sessionId}/files`)
            if (res.ok) return (await res.json()) as T
          } catch {
            /* fall through */
          }
        }
        return [] as unknown as T
      }

      if (q.includes('from terminal_executions') && q.includes('session_id')) {
        const sessionId = (_params ?? [])[0] as string
        if (sessionId) {
          try {
            const res = await fetch(`${API_BASE}/api/sessions/${sessionId}/terminal`)
            if (res.ok) return (await res.json()) as T
          } catch {
            /* fall through */
          }
        }
        return [] as unknown as T
      }

      if (q.includes('from memory_items') && q.includes('session_id')) {
        const sessionId = (_params ?? [])[0] as string
        if (sessionId) {
          // Checkpoints are memory_items with type='checkpoint'
          const isCheckpoint = q.includes("type = 'checkpoint'") || q.includes('type = ?')
          const endpoint = isCheckpoint ? 'checkpoints' : 'memory'
          try {
            const res = await fetch(`${API_BASE}/api/sessions/${sessionId}/${endpoint}`)
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
        try {
          const res = await fetch(`${API_BASE}/api/sessions/create`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name, id }),
          })
          if (!res.ok) {
            console.warn('[db-web] Failed to create session:', await res.text())
          }
        } catch (e) {
          console.warn('[db-web] Failed to create session:', e)
        }
        return { rowsAffected: 1 }
      }

      // Update session (rename, status change, etc.)
      if (q.includes('update sessions set')) {
        // The last param is the session ID (WHERE id = ?)
        const id = params?.[params.length - 1] as string
        if (id && q.includes('name =')) {
          // Parse the name from params by matching the SQL SET clause order.
          // updateSession builds: SET updated_at = ?, [name = ?,] [status = ?,] ... WHERE id = ?
          // The name value is at index 1 (after updated_at which is at index 0).
          const name = params && params.length >= 3 ? (params[1] as string) : null
          if (name) {
            try {
              await fetch(`${API_BASE}/api/sessions/${id}/rename`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ name }),
              })
            } catch (e) {
              console.warn('[db-web] Failed to rename session:', e)
            }
          }
        }
        // For status changes, updated_at-only (touchSession), etc. — no HTTP call needed
        return { rowsAffected: 1 }
      }

      // Delete session
      if (q.includes('delete from sessions')) {
        const id = params?.[0] as string
        if (id) {
          try {
            await fetch(`${API_BASE}/api/sessions/${id}`, { method: 'DELETE' })
          } catch (e) {
            console.warn('[db-web] Failed to delete session:', e)
          }
        }
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
