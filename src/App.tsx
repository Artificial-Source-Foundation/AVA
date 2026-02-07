/**
 * App Component
 * Root component with initialization logic
 *
 * Note: Preview mode is handled in index.tsx to avoid loading Node.js dependencies
 * Access the design system preview at: http://localhost:1420/?preview=true
 */

import { isTauri } from '@tauri-apps/api/core'
import { createSignal, onCleanup, onMount, Show } from 'solid-js'
import type { OnboardingData } from './components/dialogs/OnboardingDialog'
import { OnboardingScreen } from './components/dialogs/OnboardingDialog'
import { AppShell } from './components/layout'
import { validateEnv } from './config/env'
import { initDatabase } from './services/database'
import { initLogger, logError, logInfo } from './services/logger'
import { initializePlatform } from './services/platform'
import { useLayout } from './stores/layout'
import { useProject } from './stores/project'
import { useSession } from './stores/session'
import { syncAllApiKeys, useSettings } from './stores/settings'

function App() {
  const [isInitializing, setIsInitializing] = createSignal(true)
  const [initError, setInitError] = createSignal<string | null>(null)
  const [notTauri, setNotTauri] = createSignal(false)

  const { setupLayoutShortcuts } = useLayout()
  const { initializeProjects } = useProject()
  const { loadAllSessions, switchSession, createNewSession, getLastSessionId, sessions } =
    useSession()
  const { settings, updateSettings, updateProvider } = useSettings()

  onMount(async () => {
    // Register keyboard shortcuts with proper cleanup
    const cleanupShortcuts = setupLayoutShortcuts()
    onCleanup(cleanupShortcuts)

    // Guard: Estela requires the Tauri runtime
    if (!isTauri()) {
      setNotTauri(true)
      setIsInitializing(false)
      return
    }

    try {
      // Clear corrupted resizable panel sizes from localStorage
      // (caused size.endsWith crash in @corvu/resizable)
      for (const key of ['estela-sidebar-sizes', 'estela-bottom-sizes']) {
        try {
          const raw = localStorage.getItem(key)
          if (raw) {
            const parsed = JSON.parse(raw)
            if (
              !Array.isArray(parsed) ||
              parsed.length !== 2 ||
              !parsed.every((v: unknown) => typeof v === 'number' && v > 0)
            ) {
              localStorage.removeItem(key)
            }
          }
        } catch {
          localStorage.removeItem(key)
        }
      }

      // Initialize file logger first so all subsequent errors are captured
      await initLogger()
      logInfo('App', 'Initializing Estela...')

      // Validate environment variables
      validateEnv()

      // Initialize platform provider for @estela/core
      initializePlatform()

      // Sync saved API keys to the core credential store
      // (Settings UI writes to estela_settings; core reads from estela_cred_*)
      syncAllApiKeys()

      // Initialize database (runs migrations)
      await initDatabase()

      // Initialize projects first (loads projects and restores last project)
      await initializeProjects()

      // Load sessions for the current project
      await loadAllSessions()

      // Restore last session or create new one
      const lastSessionId = getLastSessionId()
      const loadedSessions = sessions()

      if (lastSessionId && loadedSessions.some((s) => s.id === lastSessionId)) {
        // Switch to last used session
        await switchSession(lastSessionId)
      } else if (loadedSessions.length > 0) {
        // Switch to most recent session
        await switchSession(loadedSessions[0].id)
      } else {
        // Create a new session
        await createNewSession()
      }
    } catch (err) {
      logError('App', 'Failed to initialize', err instanceof Error ? err.stack : String(err))
      const errorMsg = err instanceof Error ? `${err.message}\n\nStack: ${err.stack}` : String(err)
      setInitError(errorMsg)
    } finally {
      setIsInitializing(false)

      // Deferred window show — removes white flash on startup
      // Window starts with visible: false in tauri.conf.json
      try {
        const { getCurrentWindow } = await import('@tauri-apps/api/window')
        await getCurrentWindow().show()
      } catch {
        /* ignore in non-Tauri */
      }
    }
  })

  const handleOnboardingComplete = (data: OnboardingData) => {
    updateSettings({
      onboardingComplete: true,
      theme: data.theme,
      mode: data.mode,
    })
    if (data.anthropicKey) {
      updateProvider('anthropic', { apiKey: data.anthropicKey, status: 'connected', enabled: true })
    }
    if (data.openrouterKey) {
      updateProvider('openrouter', {
        apiKey: data.openrouterKey,
        status: 'connected',
        enabled: true,
      })
    }
  }

  const handleOnboardingSkip = () => {
    updateSettings({ onboardingComplete: true })
  }

  return (
    <Show
      when={!notTauri()}
      fallback={
        <div class="flex h-screen items-center justify-center bg-[var(--background)]">
          <div class="text-center max-w-md p-6">
            <div class="text-[var(--text-muted)] text-6xl mb-4">&#9670;</div>
            <h1 class="text-xl font-bold text-[var(--text-primary)] mb-2">
              Tauri Runtime Required
            </h1>
            <p class="text-[var(--text-secondary)] mb-4 text-sm leading-relaxed">
              Estela is a desktop app that requires the Tauri runtime. Run{' '}
              <code class="px-1.5 py-0.5 bg-[var(--surface-raised)] border border-[var(--border-default)] rounded text-xs font-mono">
                npm run tauri dev
              </code>{' '}
              and use the native window that opens.
            </p>
          </div>
        </div>
      }
    >
      <Show
        when={!isInitializing()}
        fallback={
          <div class="flex h-screen items-center justify-center bg-[var(--background)]">
            <div class="text-center">
              <div class="animate-spin rounded-full h-12 w-12 border-b-2 border-[var(--accent)] mx-auto" />
              <p class="mt-4 text-[var(--text-secondary)]">Initializing Estela...</p>
            </div>
          </div>
        }
      >
        <Show
          when={!initError()}
          fallback={
            <div class="flex h-screen items-center justify-center bg-[var(--background)]">
              <div class="text-center max-w-md p-6">
                <div class="text-[var(--error)] text-6xl mb-4">!</div>
                <h1 class="text-xl font-bold text-[var(--text-primary)] mb-2">
                  Initialization Error
                </h1>
                <pre class="text-[var(--text-secondary)] mb-4 text-left text-xs whitespace-pre-wrap max-w-lg overflow-auto max-h-64 bg-[var(--surface-raised)] p-3 rounded">
                  {initError()}
                </pre>
                <button
                  type="button"
                  onClick={() => window.location.reload()}
                  class="px-4 py-2 bg-[var(--accent)] text-white rounded-[var(--radius-lg)] hover:bg-[var(--accent-hover)] transition-colors"
                >
                  Retry
                </button>
              </div>
            </div>
          }
        >
          <Show
            when={settings().onboardingComplete}
            fallback={
              <OnboardingScreen
                onComplete={handleOnboardingComplete}
                onSkip={handleOnboardingSkip}
              />
            }
          >
            <AppShell />
          </Show>
        </Show>
      </Show>
    </Show>
  )
}

export default App
