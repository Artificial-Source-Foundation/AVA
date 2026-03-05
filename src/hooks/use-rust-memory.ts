import { createEffect, createResource, createSignal } from 'solid-js'
import { rustMemory } from '../services/rust-bridge'
import type { RustMemoryEntry } from '../types/rust-ipc'

const DEFAULT_RECENT_LIMIT = 20

export function useRustMemory() {
  const [memories, setMemories] = createSignal<RustMemoryEntry[]>([])
  const [loading, setLoading] = createSignal(false)
  const [error, setError] = createSignal<string | null>(null)
  const [recentLimit, setRecentLimit] = createSignal(DEFAULT_RECENT_LIMIT)

  const [recentResource, { refetch }] = createResource(recentLimit, async (limit) => {
    return rustMemory.recent(limit)
  })

  createEffect(() => {
    const data = recentResource()
    if (data) setMemories(data)
    setLoading(recentResource.loading)
    const resourceError = recentResource.error
    setError(
      resourceError
        ? resourceError instanceof Error
          ? resourceError.message
          : String(resourceError)
        : null
    )
  })

  const remember = async (key: string, value: string): Promise<RustMemoryEntry> => {
    setLoading(true)
    setError(null)
    try {
      const entry = await rustMemory.remember(key, value)
      await refetch()
      return entry
    } catch (error_) {
      setError(error_ instanceof Error ? error_.message : String(error_))
      throw error_
    } finally {
      setLoading(false)
    }
  }

  const recall = async (key: string): Promise<RustMemoryEntry | null> => {
    setLoading(true)
    setError(null)
    try {
      return await rustMemory.recall(key)
    } catch (error_) {
      setError(error_ instanceof Error ? error_.message : String(error_))
      throw error_
    } finally {
      setLoading(false)
    }
  }

  const search = async (query: string): Promise<RustMemoryEntry[]> => {
    setLoading(true)
    setError(null)
    try {
      const result = await rustMemory.search(query)
      setMemories(result)
      return result
    } catch (error_) {
      setError(error_ instanceof Error ? error_.message : String(error_))
      throw error_
    } finally {
      setLoading(false)
    }
  }

  const loadRecent = async (limit = DEFAULT_RECENT_LIMIT): Promise<RustMemoryEntry[]> => {
    setRecentLimit(limit)
    const result = await refetch()
    return result ?? []
  }

  return { memories, loading, error, remember, recall, search, loadRecent, recent: recentResource }
}
