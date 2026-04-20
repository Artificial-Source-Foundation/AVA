import type { Session, SessionWithStats } from '../types'
import { logWarn } from './logger'
import { canonicalizeSessionId } from './web-session-identity'
import { writeBrowserSession } from './web-session-write-client'

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

function isValidDuplicateSessionResponse(data: unknown): data is DuplicateSessionResponse {
  if (!data || typeof data !== 'object') return false

  const candidate = data as Record<string, unknown>
  return (
    typeof candidate.id === 'string' &&
    candidate.id.length > 0 &&
    typeof candidate.title === 'string' &&
    typeof candidate.message_count === 'number' &&
    Number.isFinite(candidate.message_count) &&
    typeof candidate.created_at === 'string' &&
    typeof candidate.updated_at === 'string'
  )
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
  try {
    const requestedName = options.name || buildCloneName(options.kind, options.sourceSessionName)
    const requestedId = crypto.randomUUID()
    const result = await writeBrowserSession<DuplicateSessionResponse>({
      frontendSessionId: options.sourceSessionId,
      action: 'duplicate',
      method: 'POST',
      jsonBody: { id: requestedId, kind: options.kind, name: requestedName },
      parseJson: true,
    })

    if (!result.ok) {
      throw new WebSessionMutationError(
        `Web ${options.kind} failed (${result.status}): ${result.errorText || result.statusText}`
      )
    }

    const data = result.data
    if (!data) {
      throw new WebSessionMutationError(`Web ${options.kind} failed: empty response payload`)
    }

    if (!isValidDuplicateSessionResponse(data)) {
      throw new WebSessionMutationError(`Web ${options.kind} failed: malformed response payload`)
    }

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
  } catch (error) {
    if (error instanceof WebSessionMutationError) {
      throw error
    }

    const message = error instanceof Error ? error.message : String(error)
    throw new WebSessionMutationError(`Web ${options.kind} failed: ${message}`)
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
