/**
 * Ollama Model Browser
 * Lists locally installed Ollama models with pull and delete actions.
 */

import { AlertTriangle, Download, Loader2, Server, Trash2, X } from 'lucide-solid'
import { type Component, createSignal, For, onMount, Show } from 'solid-js'

// ============================================================================
// Types
// ============================================================================

interface OllamaModel {
  name: string
  size: number
  family: string
  parameterSize: string
  quantizationLevel: string
  modifiedAt: string
}

interface OllamaModelBrowserProps {
  open: boolean
  onClose: () => void
  baseUrl?: string
}

// ============================================================================
// Helpers
// ============================================================================

function formatBytes(bytes: number): string {
  if (bytes < 1_000_000) return `${(bytes / 1_000).toFixed(1)} KB`
  if (bytes < 1_000_000_000) return `${(bytes / 1_000_000).toFixed(1)} MB`
  return `${(bytes / 1_000_000_000).toFixed(2)} GB`
}

// ============================================================================
// Component
// ============================================================================

export const OllamaModelBrowser: Component<OllamaModelBrowserProps> = (props) => {
  const [models, setModels] = createSignal<OllamaModel[]>([])
  const [loading, setLoading] = createSignal(false)
  const [error, setError] = createSignal<string | null>(null)
  const [pullName, setPullName] = createSignal('')
  const [pulling, setPulling] = createSignal(false)
  const [pullStatus, setPullStatus] = createSignal('')
  const [deleting, setDeleting] = createSignal<string | null>(null)
  const [confirmDelete, setConfirmDelete] = createSignal<string | null>(null)

  const baseUrl = () => props.baseUrl || 'http://localhost:11434'

  const fetchModels = async () => {
    setLoading(true)
    setError(null)
    try {
      const res = await fetch(`${baseUrl()}/api/tags`)
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      const data = (await res.json()) as {
        models: Array<{
          name: string
          size: number
          details: {
            family: string
            parameter_size: string
            quantization_level: string
          }
          modified_at: string
        }>
      }
      setModels(
        (data.models ?? []).map((m) => ({
          name: m.name,
          size: m.size,
          family: m.details?.family ?? 'unknown',
          parameterSize: m.details?.parameter_size ?? '',
          quantizationLevel: m.details?.quantization_level ?? '',
          modifiedAt: m.modified_at,
        }))
      )
    } catch (err) {
      const msg = err instanceof Error ? err.message : 'Unknown error'
      if (msg.includes('Failed to fetch') || msg.includes('NetworkError')) {
        setError('Cannot reach Ollama. Is it running?')
      } else {
        setError(`Failed to load models: ${msg}`)
      }
    } finally {
      setLoading(false)
    }
  }

  const handlePull = async () => {
    const name = pullName().trim()
    if (!name) return
    setPulling(true)
    setPullStatus('Starting pull...')
    try {
      const res = await fetch(`${baseUrl()}/api/pull`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name, stream: false }),
      })
      if (!res.ok) {
        const body = await res.text()
        throw new Error(body || `HTTP ${res.status}`)
      }
      setPullStatus('Pull complete')
      setPullName('')
      await fetchModels()
    } catch (err) {
      setPullStatus(`Pull failed: ${err instanceof Error ? err.message : 'Unknown error'}`)
    } finally {
      setPulling(false)
      setTimeout(() => setPullStatus(''), 4_000)
    }
  }

  const handleDelete = async (name: string) => {
    setDeleting(name)
    try {
      const res = await fetch(`${baseUrl()}/api/delete`, {
        method: 'DELETE',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name }),
      })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      setModels((prev) => prev.filter((m) => m.name !== name))
    } catch (err) {
      setError(`Delete failed: ${err instanceof Error ? err.message : 'Unknown error'}`)
    } finally {
      setDeleting(null)
      setConfirmDelete(null)
    }
  }

  onMount(() => {
    if (props.open) void fetchModels()
  })

  return (
    <Show when={props.open}>
      <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
        <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-5 max-w-lg w-full shadow-2xl space-y-3 max-h-[80vh] flex flex-col">
          {/* Header */}
          <div class="flex items-center justify-between">
            <div class="flex items-center gap-2">
              <Server class="w-4 h-4 text-[var(--accent)]" />
              <h3 class="text-sm font-semibold text-[var(--text-primary)]">Ollama Local Models</h3>
            </div>
            <button
              type="button"
              onClick={props.onClose}
              class="p-1 rounded-[var(--radius-sm)] hover:bg-[var(--surface-raised)] text-[var(--text-muted)] transition-colors"
            >
              <X class="w-4 h-4" />
            </button>
          </div>

          {/* Pull Model */}
          <div class="flex gap-2">
            <input
              type="text"
              placeholder="Model name (e.g. llama3.2, mistral)"
              value={pullName()}
              onInput={(e) => setPullName(e.currentTarget.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') void handlePull()
              }}
              disabled={pulling()}
              class="flex-1 px-3 py-1.5 text-xs bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] focus:border-[var(--accent)] outline-none disabled:opacity-50"
            />
            <button
              type="button"
              onClick={() => void handlePull()}
              disabled={pulling() || !pullName().trim()}
              class="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors disabled:opacity-50"
            >
              <Show when={pulling()} fallback={<Download class="w-3 h-3" />}>
                <Loader2 class="w-3 h-3 animate-spin" />
              </Show>
              Pull
            </button>
          </div>
          <Show when={pullStatus()}>
            <p
              class={`text-[10px] px-1 ${pullStatus().includes('failed') ? 'text-[var(--error)]' : 'text-[var(--text-muted)]'}`}
            >
              {pullStatus()}
            </p>
          </Show>

          {/* Error */}
          <Show when={error()}>
            <div class="flex items-center gap-2 px-3 py-2 rounded-[var(--radius-md)] bg-[var(--error-subtle)] border border-[var(--error)] text-xs text-[var(--error)]">
              <AlertTriangle class="w-3.5 h-3.5 flex-shrink-0" />
              {error()}
            </div>
          </Show>

          {/* Model List */}
          <div class="flex-1 overflow-y-auto space-y-1.5 min-h-0">
            <Show
              when={!loading()}
              fallback={
                <div class="flex items-center justify-center py-8 text-xs text-[var(--text-muted)]">
                  <Loader2 class="w-4 h-4 animate-spin mr-2" />
                  Loading models...
                </div>
              }
            >
              <Show
                when={models().length > 0}
                fallback={
                  <div class="text-center py-8 text-xs text-[var(--text-muted)]">
                    <Show when={!error()}>No models installed. Pull one above to get started.</Show>
                  </div>
                }
              >
                <For each={models()}>
                  {(model) => (
                    <div class="flex items-center gap-3 px-3 py-2 rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-raised)]">
                      <div class="flex-1 min-w-0">
                        <p class="text-xs font-medium text-[var(--text-primary)] truncate">
                          {model.name}
                        </p>
                        <div class="flex flex-wrap items-center gap-x-2 gap-y-0.5 mt-0.5">
                          <span class="text-[10px] text-[var(--text-muted)]">
                            {formatBytes(model.size)}
                          </span>
                          <Show when={model.family}>
                            <span class="text-[10px] text-[var(--text-muted)]">{model.family}</span>
                          </Show>
                          <Show when={model.parameterSize}>
                            <span class="text-[10px] text-[var(--text-muted)]">
                              {model.parameterSize}
                            </span>
                          </Show>
                          <Show when={model.quantizationLevel}>
                            <span class="text-[10px] px-1 py-0.5 rounded bg-[var(--surface-sunken)] text-[var(--text-muted)]">
                              {model.quantizationLevel}
                            </span>
                          </Show>
                        </div>
                      </div>
                      <Show
                        when={confirmDelete() !== model.name}
                        fallback={
                          <div class="flex items-center gap-1">
                            <button
                              type="button"
                              onClick={() => void handleDelete(model.name)}
                              disabled={deleting() === model.name}
                              class="px-2 py-1 text-[10px] text-white bg-[var(--error)] rounded-[var(--radius-sm)] disabled:opacity-50"
                            >
                              {deleting() === model.name ? 'Deleting...' : 'Confirm'}
                            </button>
                            <button
                              type="button"
                              onClick={() => setConfirmDelete(null)}
                              class="px-2 py-1 text-[10px] text-[var(--text-muted)] hover:text-[var(--text-secondary)]"
                            >
                              Cancel
                            </button>
                          </div>
                        }
                      >
                        <button
                          type="button"
                          onClick={() => setConfirmDelete(model.name)}
                          class="p-1.5 text-[var(--text-muted)] hover:text-[var(--error)] rounded-[var(--radius-sm)] hover:bg-[var(--error-subtle)] transition-colors"
                          title="Delete model"
                        >
                          <Trash2 class="w-3.5 h-3.5" />
                        </button>
                      </Show>
                    </div>
                  )}
                </For>
              </Show>
            </Show>
          </div>

          {/* Footer */}
          <div class="flex items-center justify-between pt-1 border-t border-[var(--border-subtle)]">
            <span class="text-[10px] text-[var(--text-muted)]">
              {models().length} model{models().length !== 1 ? 's' : ''} installed
            </span>
            <button
              type="button"
              onClick={() => void fetchModels()}
              disabled={loading()}
              class="text-[10px] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors disabled:opacity-50"
            >
              Refresh
            </button>
          </div>
        </div>
      </div>
    </Show>
  )
}
