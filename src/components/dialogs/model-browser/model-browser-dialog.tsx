/**
 * Model Browser Dialog
 *
 * Full dialog shell for browsing and selecting models.
 * Shows only enabled providers that have models.
 * Uses card/grid layout with provider logos, filters, and capability badges.
 */

import {
  type Component,
  createDeferred,
  createEffect,
  createMemo,
  createSignal,
  onCleanup,
  Show,
} from 'solid-js'
import { Dialog } from '../../ui/Dialog'
import { ModelBrowserFilters } from './model-browser-filters'
import { ModelBrowserGrid } from './model-browser-grid'
import { ModelBrowserHeader } from './model-browser-header'
import {
  aggregateModels,
  filterModels,
  findSelectedModel,
  sortModels,
} from './model-browser-helpers'
import type {
  FilterState,
  ModelBrowserDialogProps,
  ModelCapability,
  SortOption,
} from './model-browser-types'

export const ModelBrowserDialog: Component<ModelBrowserDialogProps> = (props) => {
  // Reduced from 30 to 18 for faster initial render and lighter DOM
  // 18 = 6 rows of 3 cards, sufficient for initial viewport
  const PAGE_SIZE = 18
  const [filters, setFilters] = createSignal<FilterState>({
    search: '',
    provider: null,
    capabilities: [],
    sort: 'name',
  })
  // Debounced search filter for smoother typing
  const [debouncedSearch, setDebouncedSearch] = createSignal('')
  const [visibleCount, setVisibleCount] = createSignal(PAGE_SIZE)
  let searchInputRef: HTMLInputElement | undefined

  // Aggregate once per enabled providers change (lightweight, keep sync)
  const allModels = createMemo(() => aggregateModels(props.enabledProviders()))

  // Use debounced search for filtering — defer expensive filter/sort to unblock dialog shell render
  const filteredModelsDeferred = createMemo(() => {
    const f = { ...filters(), search: debouncedSearch() }
    return sortModels(filterModels(allModels(), f), f.sort)
  })
  const filteredModels = createDeferred(() => filteredModelsDeferred(), { timeoutMs: 0 })

  const selectedBrowsable = createMemo(() =>
    findSelectedModel(allModels(), props.selectedModel(), props.selectedProvider?.() ?? null)
  )

  const selectedProvider = createMemo(() =>
    props.enabledProviders().find((p) => p.id === selectedBrowsable()?.providerId)
  )

  // Ensure selected model is in the initial visible slice (for accessibility/UX)
  const visibleModels = createMemo(() => {
    const all = filteredModels()
    const selected = selectedBrowsable()
    const limit = visibleCount()

    // If no selection or selection not in filtered results, just slice normally
    if (!selected) return all.slice(0, limit)

    const selectedIndex = all.findIndex(
      (m) => m.id === selected.id && m.providerId === selected.providerId
    )
    if (selectedIndex === -1 || selectedIndex < limit) {
      // Selected model is already in the first slice (or not in filtered results)
      return all.slice(0, limit)
    }

    // Selected model is beyond the initial slice — bring it into view by including it
    // Strategy: prepend selected model to the first slice (limit-1 items + selected)
    const selectedModel = all[selectedIndex]
    const firstSliceExcludingSelected = all
      .slice(0, limit - 1)
      .filter((m) => !(m.id === selected.id && m.providerId === selected.providerId))
    return [...firstSliceExcludingSelected, selectedModel]
  })

  // Debounce search input at 120ms for smoother filtering
  createEffect(() => {
    const searchValue = filters().search
    const timer = setTimeout(() => setDebouncedSearch(searchValue), 120)
    onCleanup(() => clearTimeout(timer))
  })

  // Focus search input when dialog opens — use rAF for immediate next-frame focus
  createEffect(() => {
    if (props.open()) {
      // Queue focus for next animation frame (faster than setTimeout)
      const rafId = requestAnimationFrame(() => {
        searchInputRef?.focus()
      })
      onCleanup(() => cancelAnimationFrame(rafId))
    }
  })

  // Reset visible count when filters change
  createEffect(() => {
    props.open()
    filters()
    setVisibleCount(PAGE_SIZE)
  })

  const handleSelect = (modelId: string, providerId: string): void => {
    props.onSelect(modelId, providerId)
    props.onOpenChange(false)
  }

  const updateFilter = <K extends keyof FilterState>(key: K, value: FilterState[K]): void => {
    setFilters((prev) => ({ ...prev, [key]: value }))
  }

  const toggleCapability = (cap: ModelCapability): void => {
    setFilters((prev) => ({
      ...prev,
      capabilities: prev.capabilities.includes(cap)
        ? prev.capabilities.filter((c) => c !== cap)
        : [...prev.capabilities, cap],
    }))
  }

  return (
    <Dialog
      open={props.open()}
      onOpenChange={props.onOpenChange}
      title="Model Browser"
      size="2xl"
      class="!max-w-[1080px] !max-h-[84vh]"
      bodyClass="!max-h-[72vh]"
      overlayStyle={{
        background: 'transparent',
      }}
      contentStyle={{
        'box-shadow': 'none',
      }}
    >
      <ModelBrowserHeader
        selectedModel={selectedBrowsable()}
        provider={selectedProvider()}
        modelCount={() => filteredModels().length}
      />

      <ModelBrowserFilters
        filters={filters()}
        providers={props.enabledProviders}
        onSearchChange={(v) => updateFilter('search', v)}
        onProviderChange={(v) => updateFilter('provider', v)}
        onCapabilityToggle={toggleCapability}
        onSortChange={(v: SortOption) => updateFilter('sort', v)}
        searchInputRef={(el: HTMLInputElement) => {
          searchInputRef = el
        }}
      />

      {/* Polite live region for result count announcements */}
      <div aria-live="polite" aria-atomic="true" class="sr-only">
        {filteredModels().length} models found
      </div>

      <ModelBrowserGrid
        models={visibleModels()}
        selectedModelId={props.selectedModel()}
        selectedProviderId={props.selectedProvider?.() ?? undefined}
        providers={props.enabledProviders()}
        onSelect={handleSelect}
      />

      <Show when={filteredModels().length > visibleCount()}>
        <div class="mt-4 flex items-center justify-center border-t border-[rgba(255,255,255,0.06)] pt-4">
          <button
            type="button"
            onClick={() => setVisibleCount((count) => count + PAGE_SIZE)}
            class="rounded-[10px] border border-[var(--border-default)] bg-[rgba(255,255,255,0.04)] px-4 py-2 text-sm text-[var(--text-primary)] transition-colors hover:bg-[rgba(255,255,255,0.07)]"
          >
            Show more models ({filteredModels().length - visibleCount()} remaining)
          </button>
        </div>
      </Show>
    </Dialog>
  )
}
