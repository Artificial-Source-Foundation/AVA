import type { Session, SessionWithStats } from '../types'
import { logWarn } from './logger'
import { buildSessionBaseEndpoint, canonicalizeSessionId } from './web-session-identity'

export type WebSessionMutationKind = 'duplicate' | 'fork'

export interface CloneSessionInWebModeOptions {
  kind: WebSessionMutationKind
  sourceSessionId: string
  sourceSessionName: string
  projectId?: string
  name?: string
}

export interface WebSessionCloneResult {
  session: Session
  stats: Pick<SessionWithStats, 'messageCount' | 'totalTokens' | 'lastPreview'>
}

interface DuplicateSessionResponse {
  id: string
  title: string
  message_count: number
  parent_session_id?: string | null
  last_preview?: string | null
  created_at: string
  updated_at: string
}

function parseTimestamp(value: string, fallback: number): number {
  const parsed = Date.parse(value)
  return Number.isFinite(parsed) ? parsed : fallback
}

export class WebSessionMutationError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'WebSessionMutationError'
  }
}

export class UnsupportedWebSessionMutationError extends WebSessionMutationError {
  constructor(message: string) {
    super(message)
    this.name = 'UnsupportedWebSessionMutationError'
  }
}

function buildCloneName(kind: WebSessionMutationKind, sourceSessionName: string): string {
  return `${sourceSessionName} (${kind === 'duplicate' ? 'copy' : 'fork'})`
}

export async function cloneSessionInWebMode(
  options: CloneSessionInWebModeOptions
): Promise<WebSessionCloneResult> {
  const requestedName = options.name || buildCloneName(options.kind, options.sourceSessionName)
  const requestedId = crypto.randomUUID()
  const endpoint = buildSessionBaseEndpoint(options.sourceSessionId, 'duplicate')
  const response = await fetch(endpoint, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: requestedId, kind: options.kind, name: requestedName }),
  })

  if (!response.ok) {
    const detail = await response.text()
    throw new WebSessionMutationError(
      `Web ${options.kind} failed (${response.status}): ${detail || response.statusText}`
    )
  }

  const data = (await response.json()) as DuplicateSessionResponse
  const now = Date.now()

  return {
    session: {
      id: data.id,
      name: data.title,
      projectId: options.projectId,
      parentSessionId: data.parent_session_id
        ? canonicalizeSessionId(data.parent_session_id)
        : undefined,
      createdAt: parseTimestamp(data.created_at, now),
      updatedAt: parseTimestamp(data.updated_at, now),
      status: 'active',
      metadata: {},
    },
    stats: {
      messageCount: data.message_count,
      totalTokens: 0,
      lastPreview: data.last_preview || '',
    },
  }
}

export async function branchSessionAtMessageInWebMode(args: {
  sessionId: string
  messageId: string
}): Promise<never> {
  logWarn('Session', 'Web branchAtMessage requires a backend-backed endpoint and is disabled', args)
  throw new UnsupportedWebSessionMutationError(
    'Branching from a specific message is not supported in web mode yet.'
  )
}
