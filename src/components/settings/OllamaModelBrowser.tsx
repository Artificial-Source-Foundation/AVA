/**
 * Ollama Model Browser
 * Lists locally installed Ollama models with pull and delete actions.
 */

import { AlertTriangle, Download, Loader2, Server, X } from 'lucide-solid'
import { type Component, createSignal, For, onMount, Show } from 'solid-js'
import { OllamaModelCard } from './ollama/OllamaModelCard'
import {
  deleteOllamaModel,
  fetchOllamaModels,
  type OllamaModel,
  pullOllamaModel,
} from './ollama/ollama-helpers'

interface OllamaModelBrowserProps {
  open: boolean
  onClose: () => void
  baseUrl?: string
}

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

  const loadModels = async () => {
    setLoading(true)
    setError(null)
    try {
      setModels(await fetchOllamaModels(baseUrl()))
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
      await pullOllamaModel(baseUrl(), name)
      setPullStatus('Pull complete')
      setPullName('')
      await loadModels()
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
      await deleteOllamaModel(baseUrl(), name)
      setModels((prev) => prev.filter((m) => m.name !== name))
    } catch (err) {
      setError(`Delete failed: ${err instanceof Error ? err.message : 'Unknown error'}`)
    } finally {
      setDeleting(null)
      setConfirmDelete(null)
    }
  }

  onMount(() => {
    if (props.open) void loadModels()
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
              onClick={() => props.onClose()}
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
              class={`text-[var(--settings-text-badge)] px-1 ${pullStatus().includes('failed') ? 'text-[var(--error)]' : 'text-[var(--text-muted)]'}`}
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
                    <OllamaModelCard
                      model={model}
                      isDeleting={deleting() === model.name}
                      isConfirmingDelete={confirmDelete() === model.name}
                      onRequestDelete={() => setConfirmDelete(model.name)}
                      onConfirmDelete={() => void handleDelete(model.name)}
                      onCancelDelete={() => setConfirmDelete(null)}
                    />
                  )}
                </For>
              </Show>
            </Show>
          </div>

          {/* Footer */}
          <div class="flex items-center justify-between pt-1 border-t border-[var(--border-subtle)]">
            <span class="text-[var(--settings-text-badge)] text-[var(--text-muted)]">
              {models().length} model{models().length !== 1 ? 's' : ''} installed
            </span>
            <button
              type="button"
              onClick={() => void loadModels()}
              disabled={loading()}
              class="text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors disabled:opacity-50"
            >
              Refresh
            </button>
          </div>
        </div>
      </div>
    </Show>
  )
}
