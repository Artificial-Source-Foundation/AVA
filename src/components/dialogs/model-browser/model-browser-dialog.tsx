/**
 * Model Browser Dialog
 *
 * Full-featured dialog for browsing and selecting AI models.
 * Models are displayed in a grouped list by provider with search filtering,
 * context window badges, and thinking capability indicators.
 *
 * Design: centered overlay, max-width 640px, dark #09090B background.
 */

import { Dialog } from '@kobalte/core/dialog'
import { Search, X } from 'lucide-solid'
import { type Component, createEffect, createMemo, createSignal, For, on, Show } from 'solid-js'
import { aggregateModels, filterModels, formatContextWindow } from './model-browser-helpers'
import { ModelBrowserRow } from './model-browser-row'
import type { BrowsableModel, FilterState, ModelBrowserDialogProps } from './model-browser-types'

export const ModelBrowserDialog: Component<ModelBrowserDialogProps> = (props) => {
  let inputRef: HTMLInputElement | undefined
  let listRef: HTMLDivElement | undefined

  const [search, setSearch] = createSignal('')
  const [selectedIndex, setSelectedIndex] = createSignal(0)

  const allModels = createMemo(() => aggregateModels(props.enabledProviders()))

  const filters = createMemo(
    (): FilterState => ({
      search: search(),
      provider: null,
      capabilities: [],
      sort: 'name',
    })
  )

  const filteredModels = createMemo(() => filterModels(allModels(), filters()))

  /** Models grouped by provider, preserving provider order */
  const grouped = createMemo(() => {
    const map = new Map<string, BrowsableModel[]>()
    for (const m of filteredModels()) {
      const list = map.get(m.providerName) ?? []
      list.push(m)
      map.set(m.providerName, list)
    }
    return [...map.entries()]
  })

  /** Flat list for keyboard navigation */
  const flatList = createMemo(() => filteredModels())

  /** Currently selected model in the list */
  const selectedBrowsable = createMemo(() => {
    const provId = props.selectedProvider?.()
    return (
      allModels().find(
        (m) => m.id === props.selectedModel() && (!provId || m.providerId === provId)
      ) ?? allModels().find((m) => m.id === props.selectedModel())
    )
  })

  // Reset state on open
  createEffect(
    on(
      () => props.open(),
      (open) => {
        if (open) {
          setSearch('')
          const idx = allModels().findIndex((m) => m.id === props.selectedModel())
          setSelectedIndex(idx >= 0 ? idx : 0)
          requestAnimationFrame(() => {
            inputRef?.focus()
            scrollToSelected()
          })
        }
      }
    )
  )

  // Clamp index when filtered results change
  createEffect(
    on(flatList, (f) => {
      setSelectedIndex((i) => Math.min(i, Math.max(0, f.length - 1)))
    })
  )

  const handleSelect = (modelId: string, providerId: string): void => {
    props.onSelect(modelId, providerId)
    props.onOpenChange(false)
  }

  const scrollToSelected = (): void => {
    requestAnimationFrame(() => {
      const active = listRef?.querySelector('[data-active="true"]')
      active?.scrollIntoView({ block: 'nearest' })
    })
  }

  const handleKeyDown = (e: KeyboardEvent): void => {
    const count = flatList().length
    if (count === 0) return

    if (e.key === 'ArrowDown') {
      e.preventDefault()
      setSelectedIndex((i) => (i + 1) % count)
      scrollToSelected()
    } else if (e.key === 'ArrowUp') {
      e.preventDefault()
      setSelectedIndex((i) => (i - 1 + count) % count)
      scrollToSelected()
    } else if (e.key === 'Enter') {
      e.preventDefault()
      const model = flatList()[selectedIndex()]
      if (model) handleSelect(model.id, model.providerId)
    }
  }

  return (
    <Dialog open={props.open()} onOpenChange={props.onOpenChange}>
      <Dialog.Portal>
        {/* Dark backdrop */}
        <Dialog.Overlay
          class="
            fixed inset-0 z-[var(--z-modal)]
            bg-black/60
            data-[expanded]:animate-in data-[expanded]:fade-in-0
            data-[closed]:animate-out data-[closed]:fade-out-0
          "
        />

        {/* Dialog content */}
        <Dialog.Content
          class="
            fixed z-[var(--z-modal)]
            top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2
            w-[min(640px,92vw)] max-h-[600px]
            flex flex-col
            bg-[#09090B] border border-[#27272A]
            rounded-[16px] shadow-2xl
            overflow-hidden
            data-[expanded]:animate-in data-[expanded]:fade-in-0 data-[expanded]:zoom-in-95
            data-[closed]:animate-out data-[closed]:fade-out-0 data-[closed]:zoom-out-95
            duration-200
          "
          onKeyDown={handleKeyDown}
        >
          {/* Title bar */}
          <div class="flex items-center justify-between px-5 pt-4 pb-2">
            <Dialog.Title class="text-sm font-semibold text-[#FAFAFA]">Model Browser</Dialog.Title>
            <Dialog.CloseButton
              class="
                p-1 rounded-[var(--radius-md)]
                text-[#71717A] hover:text-[#FAFAFA]
                hover:bg-[#18181B]
                transition-colors
              "
              aria-label="Close"
            >
              <X class="w-4 h-4" />
            </Dialog.CloseButton>
          </div>

          {/* Search input */}
          <div class="px-5 pb-3">
            <div class="relative">
              <Search class="absolute left-3 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-[#52525B]" />
              <input
                ref={inputRef}
                type="text"
                value={search()}
                onInput={(e) => setSearch(e.currentTarget.value)}
                placeholder="Search models..."
                class="
                  w-full pl-9 pr-3 py-2
                  text-[13px] text-[#FAFAFA]
                  bg-[#18181B] placeholder:text-[#52525B]
                  border border-[#27272A]
                  rounded-[10px]
                  focus:outline-none focus:border-[#A78BFA]
                  transition-colors
                "
              />
            </div>
          </div>

          {/* Model list */}
          <div
            ref={listRef}
            class="flex-1 overflow-y-auto px-2 pb-3 min-h-0"
            style={{ 'scrollbar-gutter': 'stable' }}
          >
            <Show
              when={grouped().length > 0}
              fallback={
                <div class="py-12 text-center text-xs text-[#52525B]">
                  No models match your search
                </div>
              }
            >
              <For each={grouped()}>
                {([providerName, models]) => (
                  <div class="mb-1">
                    {/* Provider group header */}
                    <div
                      class="
                        px-3 py-1.5
                        text-[12px] font-semibold text-[#3F3F46]
                        uppercase tracking-wider
                        select-none
                      "
                    >
                      {providerName}
                    </div>

                    {/* Model rows */}
                    <For each={models}>
                      {(model) => {
                        const flatIdx = () =>
                          flatList().findIndex(
                            (m) => m.id === model.id && m.providerId === model.providerId
                          )
                        const isCurrentModel = () =>
                          selectedBrowsable()?.id === model.id &&
                          selectedBrowsable()?.providerId === model.providerId
                        const isKeyboardSelected = () => flatIdx() === selectedIndex()

                        return (
                          <ModelBrowserRow
                            model={model}
                            isCurrentModel={isCurrentModel()}
                            isKeyboardSelected={isKeyboardSelected()}
                            onSelect={() => handleSelect(model.id, model.providerId)}
                            formatContext={formatContextWindow}
                          />
                        )
                      }}
                    </For>
                  </div>
                )}
              </For>
            </Show>
          </div>

          {/* Footer hints */}
          <div
            class="
              flex items-center gap-4 px-5 py-2
              border-t border-[#27272A]
              text-[10px] text-[#52525B]
              select-none
            "
          >
            <span>
              <kbd class="font-mono text-[#71717A]">&uarr;&darr;</kbd> navigate
            </span>
            <span>
              <kbd class="font-mono text-[#71717A]">Enter</kbd> select
            </span>
            <span>
              <kbd class="font-mono text-[#71717A]">Esc</kbd> close
            </span>
            <span class="ml-auto text-[#3F3F46]">
              {filteredModels().length} model{filteredModels().length !== 1 ? 's' : ''}
            </span>
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog>
  )
}
