import { createSignal, onMount } from 'solid-js'
import { rustTools } from '../services/rust-bridge'
import type { JsonValue, RustToolInfo, ToolResult } from '../types/rust-ipc'

export function useRustTools() {
  const [tools, setTools] = createSignal<RustToolInfo[]>([])
  const [loading, setLoading] = createSignal(true)
  const [error, setError] = createSignal<string | null>(null)

  onMount(async () => {
    try {
      setLoading(true)
      setError(null)
      setTools(await rustTools.list())
    } catch (error_) {
      setError(error_ instanceof Error ? error_.message : String(error_))
    } finally {
      setLoading(false)
    }
  })

  const execute = async (name: string, args: Record<string, JsonValue>): Promise<ToolResult> => {
    return rustTools.execute(name, args)
  }

  return { tools, loading, error, execute }
}
