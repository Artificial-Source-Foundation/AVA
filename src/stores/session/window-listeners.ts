const CLEANUP_KEY = '__avaSessionStateWindowCleanup__'

interface SessionStatusDetail {
  sessionId: string
  status: string
}

interface CoreSettingsDetail {
  category?: string
}

interface SessionWindowListenerDeps {
  onCompacted: () => void
  onSessionStatus: (detail: SessionStatusDetail) => void
  onBudgetUpdated: () => void
  onCoreSettingsChanged: (detail: CoreSettingsDetail | undefined) => void
}

type WindowWithCleanup = Window & {
  [CLEANUP_KEY]?: () => void
}

export function bindSessionWindowListeners(
  target: Window,
  deps: SessionWindowListenerDeps
): () => void {
  const handleCompacted = (): void => deps.onCompacted()
  const handleSessionStatus = (event: Event): void =>
    deps.onSessionStatus((event as CustomEvent<SessionStatusDetail>).detail)
  const handleBudgetUpdated = (): void => deps.onBudgetUpdated()
  const handleCoreSettingsChanged = (event: Event): void =>
    deps.onCoreSettingsChanged((event as CustomEvent<CoreSettingsDetail | undefined>).detail)

  target.addEventListener('ava:compacted', handleCompacted)
  target.addEventListener('ava:session-status', handleSessionStatus)
  target.addEventListener('ava:budget-updated', handleBudgetUpdated)
  target.addEventListener('ava:core-settings-changed', handleCoreSettingsChanged)

  return () => {
    target.removeEventListener('ava:compacted', handleCompacted)
    target.removeEventListener('ava:session-status', handleSessionStatus)
    target.removeEventListener('ava:budget-updated', handleBudgetUpdated)
    target.removeEventListener('ava:core-settings-changed', handleCoreSettingsChanged)
  }
}

export function installSessionWindowListeners(deps: SessionWindowListenerDeps): void {
  if (typeof window === 'undefined') return

  const host = window as WindowWithCleanup
  host[CLEANUP_KEY]?.()
  host[CLEANUP_KEY] = bindSessionWindowListeners(window, deps)
}
