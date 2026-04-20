/**
 * Session Branching Actions
 * Duplicate, fork, and branch sessions.
 */

import { isTauri } from '@tauri-apps/api/core'
import {
  createSession as dbCreateSession,
  insertMessages as dbInsertMessages,
  getMessages,
} from '../../services/database'
import { logInfo } from '../../services/logger'
import {
  branchSessionAtMessageInWebMode,
  cloneSessionInWebMode,
} from '../../services/web-session-mutations'
import type { Message, Session, SessionWithStats } from '../../types'
import { activatePersistedSessionMessages, finalizeSessionActivation } from './session-activation'
import { currentSession, messages, sessions, setMessages, setSessions } from './session-state'

function remapClonedMessageReferences(
  message: Message,
  idMap: Map<string, string>,
  newSessionId: string
): Message {
  const remappedMetadata = message.metadata ? { ...message.metadata } : undefined

  for (const key of ['parentId', 'parent_id']) {
    const sourceId = remappedMetadata?.[key]
    if (typeof sourceId === 'string' && idMap.has(sourceId)) {
      remappedMetadata![key] = idMap.get(sourceId)
    }
  }

  return {
    ...message,
    id: idMap.get(message.id) ?? crypto.randomUUID(),
    sessionId: newSessionId,
    metadata: remappedMetadata,
  }
}

function cloneMessagesForSession(sourceMessages: Message[], newSessionId: string): Message[] {
  const idMap = new Map(sourceMessages.map((message) => [message.id, crypto.randomUUID()]))
  return sourceMessages.map((message) => remapClonedMessageReferences(message, idMap, newSessionId))
}

// ============================================================================
// Capability Contract
// ============================================================================

/**
 * Returns whether branching at a specific message is supported in the current
 * environment. This is a UI capability check that should be used to hide or
 * disable branch affordances before user interaction.
 *
 * In Tauri (desktop) mode: branching at a message is fully supported.
 * In web mode: branching requires a backend-backed endpoint that is not yet
 * implemented, so this returns false.
 *
 * Note: The runtime will still reject branch operations in web mode as
 * defense-in-depth, but this capability check allows the UI to proactively
 * hide the affordance.
 */
export function canBranchAtMessage(): boolean {
  return isTauri()
}

// ============================================================================
// Helpers
// ============================================================================

/** After cloning a session, update signals and persist the new session as active. */
async function activateClonedSession(
  newSession: Session,
  stats: Pick<SessionWithStats, 'messageCount' | 'totalTokens' | 'lastPreview'>,
  projectId: string | undefined
): Promise<void> {
  const sessionWithStats: SessionWithStats = {
    ...newSession,
    messageCount: stats.messageCount,
    totalTokens: stats.totalTokens,
    lastPreview: stats.lastPreview,
  }
  setSessions((prev) => [sessionWithStats, ...prev])

  await activatePersistedSessionMessages(newSession, projectId, getMessages)
}

// ============================================================================
// Public API
// ============================================================================

export async function duplicateSession(sourceSessionId: string): Promise<void> {
  const source = sessions().find((s) => s.id === sourceSessionId)
  if (!source) return

  const projectId = source.projectId

  if (!isTauri()) {
    const clone = await cloneSessionInWebMode({
      kind: 'duplicate',
      sourceSessionId,
      sourceSessionName: source.name,
      projectId,
    })
    return activateClonedSession(clone.session, clone.stats, projectId)
  }

  const sourceMessages = await getMessages(sourceSessionId)
  const newName = `${source.name} (copy)`
  const newSession = await dbCreateSession(newName, projectId)

  if (sourceMessages.length > 0) {
    await dbInsertMessages(cloneMessagesForSession(sourceMessages, newSession.id))
  }

  const totalTokens = sourceMessages.reduce((sum, m) => sum + (m.tokensUsed || 0), 0)
  const lastPreview =
    sourceMessages.length > 0
      ? sourceMessages[sourceMessages.length - 1]!.content.slice(0, 100)
      : source.lastPreview

  await activateClonedSession(
    newSession,
    {
      messageCount: sourceMessages.length,
      totalTokens,
      lastPreview: lastPreview || '',
    },
    projectId
  )
}

export async function forkSession(sourceSessionId: string, name?: string): Promise<void> {
  const source = sessions().find((s) => s.id === sourceSessionId)
  if (!source) return

  const projectId = source.projectId

  if (!isTauri()) {
    const clone = await cloneSessionInWebMode({
      kind: 'fork',
      sourceSessionId,
      sourceSessionName: source.name,
      projectId,
      name,
    })
    return activateClonedSession(clone.session, clone.stats, projectId)
  }

  const forkName = name || `${source.name} (fork)`

  const sourceMessages = await getMessages(sourceSessionId)
  const newSession = await dbCreateSession(forkName, projectId, sourceSessionId)

  if (sourceMessages.length > 0) {
    await dbInsertMessages(cloneMessagesForSession(sourceMessages, newSession.id))
  }

  const totalTokens = sourceMessages.reduce((sum, m) => sum + (m.tokensUsed || 0), 0)
  const lastPreview =
    sourceMessages.length > 0
      ? sourceMessages[sourceMessages.length - 1]!.content.slice(0, 100)
      : source.lastPreview

  await activateClonedSession(
    newSession,
    {
      messageCount: sourceMessages.length,
      totalTokens,
      lastPreview: lastPreview || '',
    },
    projectId
  )
}

export async function branchAtMessage(messageId: string): Promise<void> {
  const session = currentSession()
  if (!session) return

  if (!isTauri()) {
    await branchSessionAtMessageInWebMode({ sessionId: session.id, messageId })
    return
  }

  const msgs = messages()
  const index = msgs.findIndex((m) => m.id === messageId)
  if (index === -1) return

  const projectId = session.projectId

  const messagesToCopy = msgs.slice(0, index + 1)
  const branchName = `${session.name} (branch)`
  const newSession = await dbCreateSession(branchName, projectId, session.id)
  const branchMessages = cloneMessagesForSession(messagesToCopy, newSession.id)

  await dbInsertMessages(branchMessages)

  const totalTokens = messagesToCopy.reduce((sum, m) => sum + (m.tokensUsed || 0), 0)
  const sessionWithStats: SessionWithStats = {
    ...newSession,
    messageCount: messagesToCopy.length,
    totalTokens,
    lastPreview: messagesToCopy[messagesToCopy.length - 1]?.content.slice(0, 100) || '',
  }
  setSessions((prev) => [sessionWithStats, ...prev])

  finalizeSessionActivation(newSession, {
    projectId,
    applyActiveState: () => {
      setMessages(branchMessages)
    },
  })

  logInfo('Session', `Branched at message ${index + 1}/${msgs.length} → ${newSession.id}`)
}
