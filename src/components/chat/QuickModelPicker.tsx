/**
 * Quick Model Picker
 *
 * Lightweight Ctrl+M overlay for fast model switching.
 * Shows models grouped by provider with fuzzy search.
 */

import { Dialog } from '@kobalte/core/dialog'
import { Check } from 'lucide-solid'
import { type Component, createEffect, createMemo, createSignal, For, on, Show } from 'solid-js'
import type { LLMProviderConfig } from '../../config/defaults/provider-defaults'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import {
  aggregateModels,
  formatContextWindow,
} from '../dialogs/model-browser/model-browser-helpers'

interface QuickModelPickerProps {
  open: boolean
  onClose: () => void
}

export const QuickModelPicker: Component<QuickModelPickerProps> = (props) => {
  let inputRef: HTMLInputElement | undefined
  const { selectedModel, selectedProvider, setSelectedModel } = useSession()
  const { settings } = useSettings()
  const [query, setQuery] = createSignal('')
  const [selectedIndex, setSelectedIndex] = createSignal(0)

  const matchesCurrentSelection = (modelId: string, providerId: string): boolean => {
    return modelId === selectedModel() && (!selectedProvider() || providerId === selectedProvider())
  }

  const enabledProviders = createMemo(() =>
    Object.values(settings().providers).filter(
      (p): p is LLMProviderConfig => p.enabled && p.models.length > 0
    )
  )

  const allModels = createMemo(() => aggregateModels(enabledProviders()))

  const filtered = createMemo(() => {
    const q = query().toLowerCase().trim()
    if (!q) return allModels()
    return allModels().filter(
      (m) =>
        m.name.toLowerCase().includes(q) ||
        m.id.toLowerCase().includes(q) ||
        m.providerName.toLowerCase().includes(q)
    )
  })

  // Group by provider
  const grouped = createMemo(() => {
    const map = new Map<
      string,
      typeof filtered extends () => infer T ? (T extends Array<infer U> ? U[] : never) : never
    >()
    for (const m of filtered()) {
      const list = map.get(m.providerName) ?? []
      list.push(m)
      map.set(m.providerName, list)
    }
    return [...map.entries()]
  })

  // Flat list for keyboard navigation
  const flatList = createMemo(() => filtered())

  // Reset on open
  createEffect(
    on(
      () => props.open,
      (open) => {
        if (open) {
          setQuery('')
          // Pre-select current model
          const idx = allModels().findIndex((m) => matchesCurrentSelection(m.id, m.providerId))
          setSelectedIndex(idx >= 0 ? idx : 0)
          requestAnimationFrame(() => inputRef?.focus())
        }
      }
    )
  )

  createEffect(
    on(filtered, (f) => {
      setSelectedIndex((i) => Math.min(i, Math.max(0, f.length - 1)))
    })
  )

  const handleSelect = (modelId: string, providerId: string) => {
    setSelectedModel(modelId, providerId)
    props.onClose()
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    const count = flatList().length
    if (count === 0) return
    if (e.key === 'ArrowDown') {
      e.preventDefault()
      setSelectedIndex((i) => (i + 1) % count)
    } else if (e.key === 'ArrowUp') {
      e.preventDefault()
      setSelectedIndex((i) => (i - 1 + count) % count)
    } else if (e.key === 'Enter') {
      e.preventDefault()
      const model = flatList()[selectedIndex()]
      if (model) handleSelect(model.id, model.providerId)
    }
  }

  return (
    <Dialog open={props.open} onOpenChange={(open) => !open && props.onClose()}>
      <Dialog.Portal>
        <Dialog.Overlay
          class="fixed inset-0 z-[var(--z-modal)]"
          style={{ background: 'rgba(0,0,0,0.25)' }}
        />
        <Dialog.Content
          class="
            fixed z-[var(--z-modal)]
            top-[15%] left-1/2 -translate-x-1/2
            w-[min(520px,90vw)]
            overflow-hidden
          "
          style={{
            background: 'var(--modal-surface)',
            border: '1px solid var(--modal-border)',
            'border-radius': 'var(--modal-radius-sm)',
            'box-shadow': 'var(--modal-shadow)',
          }}
          onKeyDown={handleKeyDown}
        >
          {/* Search */}
          <div class="px-3 py-2" style={{ 'border-bottom': '1px solid var(--modal-border)' }}>
            <input
              ref={inputRef}
              type="text"
              value={query()}
              onInput={(e) => setQuery(e.currentTarget.value)}
              placeholder="Search models..."
              class="
                w-full bg-transparent text-sm text-[var(--text-primary)]
                placeholder:text-[var(--text-muted)]
                focus:outline-none
              "
            />
          </div>

          {/* Model list */}
          <div class="max-h-[400px] overflow-y-auto py-1 scroll-smooth">
            <Show
              when={grouped().length > 0}
              fallback={
                <div class="px-4 py-6 text-center text-xs text-[var(--text-muted)]">
                  No models found
                </div>
              }
            >
              <For each={grouped()}>
                {([providerName, models]) => (
                  <div>
                    <div class="px-3 py-1 text-[9px] font-semibold text-[var(--text-muted)] uppercase tracking-wider">
                      {providerName}
                    </div>
                    <For each={models}>
                      {(model) => {
                        const flatIdx = () =>
                          flatList().findIndex(
                            (m) => m.id === model.id && m.providerId === model.providerId
                          )
                        const isCurrent = () => matchesCurrentSelection(model.id, model.providerId)
                        return (
                          <button
                            type="button"
                            onClick={() => handleSelect(model.id, model.providerId)}
                            class="
                              w-full flex items-center gap-3 px-3 py-1.5 text-left
                              transition-colors
                            "
                            style={{
                              background:
                                flatIdx() === selectedIndex()
                                  ? 'var(--dropdown-item-active-bg)'
                                  : 'transparent',
                            }}
                          >
                            <div class="flex-1 min-w-0">
                              <span class="text-xs text-[var(--text-primary)]">{model.name}</span>
                              <Show when={model.contextWindow !== null}>
                                <span class="ml-2 text-[10px] text-[var(--text-muted)]">
                                  {formatContextWindow(model.contextWindow)}
                                </span>
                              </Show>
                            </div>
                            <div class="flex items-center gap-1.5 flex-shrink-0">
                              <Show when={model.pricing}>
                                <span class="text-[9px] text-[var(--text-muted)]">
                                  {model.pricing!.input === 0
                                    ? 'Free'
                                    : `$${model.pricing!.input}/M`}
                                </span>
                              </Show>
                              <Show when={isCurrent()}>
                                <Check class="w-3.5 h-3.5 text-[var(--accent)]" />
                              </Show>
                            </div>
                          </button>
                        )
                      }}
                    </For>
                  </div>
                )}
              </For>
            </Show>
          </div>

          {/* Footer */}
          <div class="px-3 py-1.5 border-t border-[var(--border-subtle)] text-[9px] text-[var(--text-muted)] flex gap-3">
            <span>
              <kbd class="font-mono">↑↓</kbd> navigate
            </span>
            <span>
              <kbd class="font-mono">Enter</kbd> select
            </span>
            <span>
              <kbd class="font-mono">Esc</kbd> close
            </span>
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog>
  )
}
