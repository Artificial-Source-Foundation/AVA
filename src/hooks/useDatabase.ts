import { createSignal, onMount } from 'solid-js'
import { initDatabase } from '../services/database'

// Hook to ensure database is initialized
export function useDatabase() {
  const [isReady, setIsReady] = createSignal(false)
  const [error, setError] = createSignal<Error | null>(null)

  onMount(async () => {
    try {
      await initDatabase()
      setIsReady(true)
    } catch (e) {
      setError(e instanceof Error ? e : new Error(String(e)))
    }
  })

  return { isReady, error }
}
