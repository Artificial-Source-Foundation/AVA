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
import { SplashScreen } from './components/SplashScreen'
import { validateEnv } from './config/env'
import { initCoreBridge } from './services/core-bridge'
import { initDatabase } from './services/database'
import { initLogger, logError, logInfo } from './services/logger'
import { initializePlatform } from './services/platform'
import { useLayout } from './stores/layout'
import { useProject } from './stores/project'
import { useSession } from './stores/session'
import { applyAppearance, pushSettingsToCore, syncAllApiKeys, useSettings } from './stores/settings'
import { useShortcuts } from './stores/shortcuts'

const SPLASH_MIN_MS = 800

function App() {
  const [isInitializing, setIsInitializing] = createSignal(true)
  const [initError, setInitError] = createSignal<string | null>(null)
  const [notTauri, setNotTauri] = createSignal(false)
  const [splashStatus, setSplashStatus] = createSignal('')

  const { toggleSidebar, toggleSettings, toggleBottomPanel } = useLayout()
  const { initializeProjects } = useProject()
  const { loadAllSessions, switchSession, createNewSession, getLastSessionId, sessions } =
    useSession()
  const { settings, updateSettings, updateProvider } = useSettings()
  const { registerAction, setupShortcutListener } = useShortcuts()

  onMount(async () => {
    // Apply appearance settings (mode, accent, scale, font) to DOM immediately
    applyAppearance()

    // Register shortcut actions and install global listener
    registerAction('toggle-sidebar', toggleSidebar)
    registerAction('toggle-settings', toggleSettings)
    registerAction('toggle-bottom-panel', toggleBottomPanel)
    registerAction('new-chat', () => createNewSession())
    const cleanupShortcuts = setupShortcutListener()
    onCleanup(cleanupShortcuts)

    // Guard: Estela requires the Tauri runtime
    if (!isTauri()) {
      setNotTauri(true)
      setIsInitializing(false)
      return
    }

    // Show window early so the splash screen is visible during initialization.
    // The splash covers the full viewport, so there's no white flash.
    try {
      const { getCurrentWindow } = await import('@tauri-apps/api/window')
      await getCurrentWindow().show()
    } catch {
      /* ignore in non-Tauri */
    }

    const splashStart = Date.now()

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

      setSplashStatus('Starting logger...')
      await initLogger()
      logInfo('App', 'Initializing Estela...')

      validateEnv()

      setSplashStatus('Initializing platform...')
      initializePlatform()
      syncAllApiKeys()

      setSplashStatus('Initializing core engine...')
      const openAIKey = settings().providers.find((p) => p.id === 'openai')?.apiKey
      const cleanupCore = await initCoreBridge({
        contextLimit: 200_000,
        openAIApiKey: openAIKey,
      })
      onCleanup(cleanupCore)
      pushSettingsToCore()

      setSplashStatus('Loading database...')
      await initDatabase()

      setSplashStatus('Loading projects...')
      await initializeProjects()

      setSplashStatus('Restoring session...')
      await loadAllSessions()

      // Restore last session or create new one
      const lastSessionId = getLastSessionId()
      const loadedSessions = sessions()

      if (lastSessionId && loadedSessions.some((s) => s.id === lastSessionId)) {
        await switchSession(lastSessionId)
      } else if (loadedSessions.length > 0) {
        await switchSession(loadedSessions[0].id)
      } else {
        await createNewSession()
      }
    } catch (err) {
      logError('App', 'Failed to initialize', err instanceof Error ? err.stack : String(err))
      const errorMsg = err instanceof Error ? `${err.message}\n\nStack: ${err.stack}` : String(err)
      setInitError(errorMsg)
    } finally {
      // Ensure splash shows for at least SPLASH_MIN_MS so it doesn't just flash
      const elapsed = Date.now() - splashStart
      const remaining = SPLASH_MIN_MS - elapsed
      if (remaining > 0) {
        await new Promise((r) => setTimeout(r, remaining))
      }
      setIsInitializing(false)
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
      <SplashScreen visible={isInitializing()} status={splashStatus()} />
      <Show when={!isInitializing()}>
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
