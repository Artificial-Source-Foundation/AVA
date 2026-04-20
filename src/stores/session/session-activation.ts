import { STORAGE_KEYS } from '../../config/constants'
import type { Message, Session } from '../../types'
import { setLastSessionForProject } from '../session-persistence'
import { setCurrentSession, setIsLoadingMessages, setMessages } from './session-state'

export interface FinalizeSessionActivationOptions {
  projectId?: string | null
  persistSelection?: boolean
  settleLoading?: boolean
  applyActiveState?: () => void
}

export interface ActivatePersistedSessionOptions<TLoaded> {
  projectId?: string | null
  startLoading?: boolean
  persistSelection?: boolean
  isCurrent?: () => boolean
  beforeLoad?: () => void
  load: (sessionId: string) => Promise<TLoaded>
  applyLoaded: (loaded: TLoaded) => void
  applyLoadFallback?: () => void
  onLoadError?: (error: unknown) => void
  shouldSettle?: () => boolean
}

export function persistSelectedSession(
  projectId: string | null | undefined,
  sessionId: string
): void {
  localStorage.setItem(STORAGE_KEYS.LAST_SESSION, sessionId)
  setLastSessionForProject(projectId, sessionId)
}

export function finalizeSessionActivation(
  session: Session,
  options: FinalizeSessionActivationOptions = {}
): void {
  const { projectId, persistSelection = true, settleLoading = true, applyActiveState } = options

  setCurrentSession(session)
  applyActiveState?.()

  if (settleLoading) {
    setIsLoadingMessages(false)
  }

  if (persistSelection) {
    persistSelectedSession(projectId, session.id)
  }
}

export async function activatePersistedSession<TLoaded>(
  session: Session,
  options: ActivatePersistedSessionOptions<TLoaded>
): Promise<TLoaded | undefined> {
  const {
    projectId,
    startLoading = true,
    persistSelection = true,
    isCurrent,
    beforeLoad,
    load,
    applyLoaded,
    applyLoadFallback,
    onLoadError,
    shouldSettle,
  } = options
  const isCurrentSession = () => isCurrent?.() ?? true

  setCurrentSession(session)
  setIsLoadingMessages(startLoading)

  try {
    beforeLoad?.()
    const loaded = await load(session.id)
    if (isCurrentSession()) {
      finalizeSessionActivation(session, {
        persistSelection,
        settleLoading: false,
        projectId,
        applyActiveState: () => applyLoaded(loaded),
      })
    }

    return loaded
  } catch (error) {
    onLoadError?.(error)
    if (isCurrentSession()) {
      finalizeSessionActivation(session, {
        persistSelection,
        settleLoading: false,
        projectId,
        applyActiveState: applyLoadFallback,
      })
    }

    return undefined
  } finally {
    if (isCurrentSession() && (shouldSettle?.() ?? true)) {
      setIsLoadingMessages(false)
    }
  }
}

export async function activatePersistedSessionMessages(
  session: Session,
  projectId: string | null | undefined,
  loadMessages: (sessionId: string) => Promise<Message[]>
): Promise<Message[] | undefined> {
  return activatePersistedSession(session, {
    projectId,
    load: loadMessages,
    applyLoaded: (loadedMessages) => setMessages(loadedMessages),
    applyLoadFallback: () => setMessages([]),
  })
}
