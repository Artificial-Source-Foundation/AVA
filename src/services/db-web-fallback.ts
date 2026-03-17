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
          if (!res.ok) return [] as unknown as T
          const sessions = await res.json()
          // Map API response to the row format expected by mapSessionRow
          return sessions.map((s: Record<string, unknown>) => ({
            id: s.id,
            name: s.title,
            project_id: null,
            parent_session_id: null,
            slug: null,
            busy_since: null,
            created_at: new Date(s.created_at as string).getTime(),
            updated_at: new Date(s.updated_at as string).getTime(),
            status: 'active',
            metadata: null,
            message_count: s.message_count ?? 0,
            total_tokens: 0,
            total_cost: 0,
            last_preview: null,
          })) as unknown as T
        } catch {
          return [] as unknown as T
        }
      }

      // Message listing for a session
      if (q.includes('from messages') && q.includes('session_id')) {
        const params = _params ?? []
        const sessionId = params[0] as string
        if (!sessionId) return [] as unknown as T
        try {
          const res = await fetch(`${API_BASE}/api/sessions/${sessionId}`)
          if (!res.ok) return [] as unknown as T
          const detail = await res.json()
          // Map to db row format expected by mapDbMessages
          return (detail.messages ?? []).map((m: Record<string, unknown>) => ({
            id: m.id,
            session_id: sessionId,
            role: m.role,
            content: m.content,
            agent_id: null,
            created_at: new Date(m.timestamp as string).getTime(),
            tokens_used: 0,
            cost_usd: null,
            model: null,
            metadata: '{}',
          })) as unknown as T
        } catch {
          return [] as unknown as T
        }
      }

      // Schema version check
      if (q.includes('sqlite_master') || q.includes('schema_version')) {
        return [] as unknown as T
      }

      // Default: return empty array for any other select
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
        // The last param is the session ID
        const id = params?.[params.length - 1] as string
        if (id && q.includes('name =')) {
          // Find the name param - it's the one after 'updated_at'
          // updateSession sets: updated_at, then optional name, status, metadata, slug, busy_since
          // First value is always Date.now() (updated_at), id is last
          const nameIdx = params?.findIndex(
            (p, i) => i > 0 && i < (params?.length ?? 0) - 1 && typeof p === 'string' && p !== id
          )
          const name = nameIdx !== undefined && nameIdx >= 0 ? (params?.[nameIdx] as string) : null
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

      // Insert message
      if (q.includes('insert into messages')) {
        const sessionId = params?.[1] as string
        const role = params?.[2] as string
        const content = params?.[3] as string
        if (sessionId && content) {
          try {
            await fetch(`${API_BASE}/api/sessions/${sessionId}/message`, {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({ content, role: role || 'user' }),
            })
          } catch (e) {
            console.warn('[db-web] Failed to add message:', e)
          }
        }
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
      return { rowsAffected: 0 }
    },
  }
}
