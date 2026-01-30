/**
 * App Component
 * Root component with initialization logic
 */

import { createSignal, onMount, Show } from 'solid-js'
import { ChatView } from './components/chat'
import { AppShell } from './components/layout'
import { validateEnv } from './config/env'
import { initDatabase } from './services/database'
import { useSession } from './stores/session'

function App() {
  const [isInitializing, setIsInitializing] = createSignal(true)
  const [initError, setInitError] = createSignal<string | null>(null)

  const { loadAllSessions, switchSession, createNewSession, getLastSessionId, sessions } =
    useSession()

  onMount(async () => {
    try {
      // Validate environment variables
      validateEnv()

      // Initialize database (runs migrations)
      await initDatabase()

      // Load all sessions
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
        <div class="flex h-screen items-center justify-center bg-gray-900">
          <div class="text-center">
            <div class="animate-spin rounded-full h-12 w-12 border-b-2 border-blue-500 mx-auto" />
            <p class="mt-4 text-gray-400">Initializing Estela...</p>
          </div>
        </div>
      }
    >
      <Show
        when={!initError()}
        fallback={
          <div class="flex h-screen items-center justify-center bg-gray-900">
            <div class="text-center max-w-md p-6">
              <div class="text-red-500 text-6xl mb-4">!</div>
              <h1 class="text-xl font-bold text-white mb-2">Initialization Error</h1>
              <p class="text-gray-400 mb-4">{initError()}</p>
              <button
                type="button"
                onClick={() => window.location.reload()}
                class="px-4 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-500"
              >
                Retry
              </button>
            </div>
          </div>
        }
      >
        <AppShell>
          <ChatView />
        </AppShell>
      </Show>
    </Show>
  )
}

export default App
