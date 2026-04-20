/**
 * Desktop Session Storage
 *
 * Implements core-v2 SessionStorage interface backed by the desktop SQLite database.
 * This bridges core-v2's SessionManager with the desktop app's existing session tables,
 * ensuring both systems share the same persistence layer.
 */

import type { Message } from '../types'

/** Local content block types (replaces @ava/core-v2/llm import) */
interface TextBlock {
  type: 'text'
  text: string
}

interface ToolUseBlock {
  type: 'tool_use'
  id: string
  name: string
  input: unknown
}

type ContentBlock = TextBlock | ToolUseBlock | { type: string; [key: string]: unknown }

/** Local session types (replaces @ava/core-v2/session import) */
interface SessionState {
  id: string
  name?: string
  slug?: string
  messages: Array<{ role: 'user' | 'assistant'; content: ContentBlock[] | string }>
  workingDirectory: string
  toolCallCount: number
  tokenStats: {
    inputTokens: number
    outputTokens: number
    messages: Map<string, unknown>
  }
  openFiles: Map<string, unknown>
  env: Record<string, unknown>
  createdAt: number
  updatedAt: number
  status: string
  parentSessionId?: string
}

interface SessionStorage {
  save(session: SessionState): Promise<void>
  load(id: string): Promise<SessionState | null>
  delete(id: string): Promise<boolean>
  list(): Promise<Array<{ id: string; name?: string; updatedAt: number }>>
  loadAll(): Promise<SessionState[]>
}

import {
  createSession,
  deleteSession,
  getDb,
  getMessages,
  getSessionsWithStats,
  updateSession,
} from './database'

/**
 * Convert desktop Message[] to core-v2 ChatMessage[] format.
 */
function messagesToChatMessages(messages: Message[]): SessionState['messages'] {
  return messages.map((msg) => {
    const blocks: ContentBlock[] = [{ type: 'text', text: msg.content } satisfies TextBlock]

    // Append tool_use blocks from metadata
    if (msg.toolCalls) {
      for (const tc of msg.toolCalls) {
        blocks.push({
          type: 'tool_use',
          id: tc.id,
          name: tc.name,
          input: tc.args,
        } satisfies ToolUseBlock)
      }
    }

    return {
      role: msg.role as 'user' | 'assistant',
      content: blocks,
    }
  })
}

/**
 * Adapts the desktop SQLite database to core-v2's SessionStorage interface.
 */
export class DesktopSessionStorage implements SessionStorage {
  async save(session: SessionState): Promise<void> {
    const db = await getDb()

    // Check if session already exists
    const existing = await db.select<Array<{ id: string }>>(
      'SELECT id FROM sessions WHERE id = ?',
      [session.id]
    )

    if (existing.length === 0) {
      // Create new session row
      await createSession(session.name ?? 'New Chat')
    }

    // Update session fields
    await updateSession(session.id, {
      name: session.name ?? 'New Chat',
      status:
        session.status === 'active'
          ? 'active'
          : session.status === 'archived'
            ? 'archived'
            : session.status === 'completed'
              ? 'completed'
              : 'active',
      slug: session.slug,
      busySince: session.status === 'busy' ? Date.now() : null,
    })
  }

  async load(id: string): Promise<SessionState | null> {
    const db = await getDb()

    const rows = await db.select<Array<Record<string, unknown>>>(
      'SELECT * FROM sessions WHERE id = ?',
      [id]
    )
    if (rows.length === 0) return null

    const row = rows[0]
    const messages = await getMessages(id)
    const chatMessages = messagesToChatMessages(messages)

    return {
      id: row.id as string,
      name: row.name as string,
      slug: (row.slug as string) || undefined,
      messages: chatMessages,
      workingDirectory: '.',
      toolCallCount: 0,
      tokenStats: {
        inputTokens: messages.reduce((sum, m) => sum + (m.tokensUsed ?? 0), 0),
        outputTokens: 0,
        messages: new Map(),
      },
      openFiles: new Map(),
      env: {},
      createdAt: row.created_at as number,
      updatedAt: row.updated_at as number,
      status: (row.status as string) === 'archived' ? 'archived' : 'active',
      parentSessionId: (row.parent_session_id as string) || undefined,
    }
  }

  async delete(id: string): Promise<boolean> {
    try {
      await deleteSession(id)
      return true
    } catch {
      return false
    }
  }

  async list(): Promise<Array<{ id: string; name?: string; updatedAt: number }>> {
    const sessions = await getSessionsWithStats()
    return sessions.map((s) => ({
      id: s.id,
      name: s.name,
      updatedAt: s.updatedAt,
    }))
  }

  async loadAll(): Promise<SessionState[]> {
    const sessions = await getSessionsWithStats()
    const results: SessionState[] = []
    for (const s of sessions) {
      const state = await this.load(s.id)
      if (state) results.push(state)
    }
    return results
  }
}
