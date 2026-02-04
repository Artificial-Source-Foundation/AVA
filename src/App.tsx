/**
 * App Component
 * Root component with initialization logic
 *
 * Note: Preview mode is handled in index.tsx to avoid loading Node.js dependencies
 * Access the design system preview at: http://localhost:1420/?preview=true
 */

import { createSignal, onMount, Show } from 'solid-js'
import { AppShell } from './components/layout'
import { validateEnv } from './config/env'
import { initDatabase } from './services/database'
import { initializePlatform } from './services/platform'
import { useProject } from './stores/project'
import { useSession } from './stores/session'

function App() {
  const [isInitializing, setIsInitializing] = createSignal(true)
  const [initError, setInitError] = createSignal<string | null>(null)

  const { initializeProjects } = useProject()
  const { loadAllSessions, switchSession, createNewSession, getLastSessionId, sessions } =
    useSession()

  onMount(async () => {
    try {
      // Validate environment variables
      validateEnv()

      // Initialize platform provider for @estela/core
      initializePlatform()

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
      console.error('Failed to initialize app:', err)
      setInitError(err instanceof Error ? err.message : 'Unknown error')
    } finally {
      setIsInitializing(false)
    }
  })

  return (
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
              <p class="text-[var(--text-secondary)] mb-4">{initError()}</p>
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
        <AppShell />
      </Show>
    </Show>
  )
}

export default App
