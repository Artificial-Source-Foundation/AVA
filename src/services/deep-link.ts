/**
 * Deep Link Protocol Handler (ava://)
 *
 * Parses and handles deep link URLs for navigation within the app:
 * - ava://settings/<tab>  -> Open settings modal to specific tab
 * - ava://session/<id>    -> Switch to a session by ID
 * - ava://workflow/<id>   -> Trigger a workflow by ID
 *
 * Uses Tauri's `onOpenUrl` (plugin-deep-link) when available,
 * falls back to custom event listener for in-app navigation.
 */

import { logError, logInfo } from './logger'

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

interface Disposable {
  dispose: () => void
}

interface DeepLinkRoute {
  type: 'settings' | 'session' | 'workflow'
  id: string
}

// ---------------------------------------------------------------------------
// URL Parsing
// ---------------------------------------------------------------------------

/**
 * Parse an ava:// deep link URL into a route object.
 * Returns null if the URL is invalid or not an ava:// URL.
 */
export function parseDeepLink(url: string): DeepLinkRoute | null {
  try {
    // Normalize: handle both ava:// and ava:/ forms
    const normalized = url.trim()
    if (!normalized.startsWith('ava://')) return null

    // Strip protocol and split path
    const path = normalized.slice('ava://'.length).replace(/\/+$/, '')
    const segments = path.split('/').filter(Boolean)

    if (segments.length < 2) return null

    const [type, ...rest] = segments
    const id = rest.join('/')

    if (type === 'settings' || type === 'session' || type === 'workflow') {
      return { type, id }
    }

    return null
  } catch {
    return null
  }
}

// ---------------------------------------------------------------------------
// Navigation Handler
// ---------------------------------------------------------------------------

/**
 * Handle a deep link URL by navigating to the appropriate view.
 * Dispatches custom events that App.tsx and stores listen for.
 */
export function handleDeepLink(url: string): void {
  const route = parseDeepLink(url)
  if (!route) {
    logError('DeepLink', `Invalid deep link URL: ${url}`)
    return
  }

  logInfo('DeepLink', `Navigating to ${route.type}/${route.id}`)

  switch (route.type) {
    case 'settings':
      // Open settings modal to the specified tab
      window.dispatchEvent(new CustomEvent('ava:deep-link-settings', { detail: { tab: route.id } }))
      break

    case 'session':
      // Switch to the specified session
      window.dispatchEvent(
        new CustomEvent('ava:deep-link-session', { detail: { sessionId: route.id } })
      )
      break

    case 'workflow':
      // Trigger the specified workflow
      window.dispatchEvent(
        new CustomEvent('ava:deep-link-workflow', { detail: { workflowId: route.id } })
      )
      break
  }
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

/**
 * Initialize deep link listeners.
 * Attempts to use @tauri-apps/plugin-deep-link if available,
 * and always listens for in-app custom events.
 *
 * Returns a Disposable for cleanup.
 */
export function initDeepLinks(): Disposable {
  const cleanups: Array<() => void> = []
  let disposed = false

  // 1. Try Tauri deep-link plugin (handles OS-level ava:// URLs)
  tryTauriDeepLink().then((cleanup) => {
    if (!cleanup) return
    if (disposed) {
      cleanup()
      return
    }
    cleanups.push(cleanup)
  })

  // 2. In-app custom event listener (for programmatic deep links)
  const handler = (e: Event) => {
    const url = (e as CustomEvent<{ url: string }>).detail.url
    if (url) handleDeepLink(url)
  }
  window.addEventListener('ava:deep-link', handler)
  cleanups.push(() => window.removeEventListener('ava:deep-link', handler))

  // 3. Wire up navigation events to stores
  const settingsHandler = (e: Event) => {
    const { tab } = (e as CustomEvent<{ tab: string }>).detail
    // Open settings and switch to the specified tab
    const layoutModule = import('../stores/layout')
    layoutModule.then(({ useLayout }) => {
      const { openSettings } = useLayout()
      openSettings()
      // Dispatch tab switch event for SettingsDialog to pick up
      window.dispatchEvent(new CustomEvent('ava:settings-tab', { detail: { tab } }))
    })
  }
  window.addEventListener('ava:deep-link-settings', settingsHandler)
  cleanups.push(() => window.removeEventListener('ava:deep-link-settings', settingsHandler))

  const sessionHandler = (e: Event) => {
    const { sessionId } = (e as CustomEvent<{ sessionId: string }>).detail
    const sessionModule = import('../stores/session')
    sessionModule.then(({ useSession }) => {
      const session = useSession()
      session.switchSession(sessionId)
    })
  }
  window.addEventListener('ava:deep-link-session', sessionHandler)
  cleanups.push(() => window.removeEventListener('ava:deep-link-session', sessionHandler))

  const workflowHandler = (e: Event) => {
    const { workflowId } = (e as CustomEvent<{ workflowId: string }>).detail
    const workflowModule = import('../stores/workflows')
    workflowModule.then(({ useWorkflows }) => {
      const { workflows, applyWorkflow } = useWorkflows()
      const wf = workflows().find((w) => w.id === workflowId)
      if (wf) {
        applyWorkflow(wf)
      } else {
        logError('DeepLink', `Workflow not found: ${workflowId}`)
      }
    })
  }
  window.addEventListener('ava:deep-link-workflow', workflowHandler)
  cleanups.push(() => window.removeEventListener('ava:deep-link-workflow', workflowHandler))

  logInfo('DeepLink', 'Deep link listeners initialized')

  return {
    dispose: () => {
      disposed = true
      for (const cleanup of cleanups) cleanup()
      cleanups.length = 0
    },
  }
}

// ---------------------------------------------------------------------------
// Tauri Plugin Integration
// ---------------------------------------------------------------------------

// Opaque module name so Vite cannot statically resolve it
const DEEP_LINK_PKG = ['@tauri-apps', 'plugin-deep-link'].join('/')

async function tryTauriDeepLink(): Promise<(() => void) | null> {
  try {
    const { onOpenUrl } = await import(/* @vite-ignore */ DEEP_LINK_PKG)
    const unlisten = await onOpenUrl((urls: string[]) => {
      for (const url of urls) {
        handleDeepLink(url)
      }
    })
    logInfo('DeepLink', 'Tauri deep-link plugin registered')
    return unlisten
  } catch {
    // Plugin not available — that's fine, in-app events still work
    logInfo('DeepLink', 'Tauri deep-link plugin not available, using in-app events only')
    return null
  }
}
